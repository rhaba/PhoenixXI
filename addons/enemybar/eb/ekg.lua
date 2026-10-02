--[[
    enemybar - "Condition" heart monitor, after the Resident Evil status screens (RE2, Code:
    Veronica, RE0): a dark scanlined panel, a glowing trace swept left to right with a fading
    tail, and Fine / Caution / Danger in the corner.

    The mob's HP drives it:
        color      green (Fine, 60%+) -> yellow / orange (Caution) -> red (Danger, under 25%)
        heart rate ~60 BPM at full HP, rising to ~180 near death, with an irregular, jittery
                   trace in Danger
        each beat  flashes the panel border
        dead       flatline

    Works like a real monitor: a write head sweeps across the panel, writing one sample per
    pixel column it passes, so a change in heart rate shows up as the head moves on rather than
    redrawing the whole trace. State is kept per monitor id (one per mob per bar).
]]

local imgui = require('imgui');

local M = {};

local SWEEP_SECONDS = 2.4;   -- time for the head to cross the panel
local STEP = 2;              -- pixels per trace segment
local states = {};

-- Resident Evil's condition colors.
local FINE    = { 0.30, 1.00, 0.45 };
local CAUTION = { 1.00, 0.86, 0.20 };
local WARN    = { 1.00, 0.55, 0.12 };
local DANGER  = { 1.00, 0.16, 0.16 };
local DEAD    = { 0.55, 0.55, 0.55 };

local function lerp(a, b, t) return a + (b - a) * t; end
local function mix(c1, c2, t)
    t = math.max(0, math.min(1, t));
    return { lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t), lerp(c1[3], c2[3], t) };
end

-- Color and label for an HP percent.
function M.Condition(hpp)
    if hpp <= 0 then return DEAD, 'Dead'; end
    if hpp >= 60 then return FINE, 'Fine'; end
    if hpp >= 40 then return mix(CAUTION, FINE, (hpp - 40) / 20), 'Caution'; end
    if hpp >= 25 then return mix(WARN, CAUTION, (hpp - 25) / 15), 'Caution'; end
    return mix(DANGER, WARN, (hpp - 10) / 15), 'Danger';
end

function M.Bpm(hpp)
    if hpp <= 0 then return 0; end
    return 60 + 120 * (1 - hpp / 100) ^ 1.3;
end

-- One heartbeat, phase 0..1: a small dip, a sharp spike, then the decaying wobble the RE
-- monitors show. Returns -1..1 (up is positive).
local function gauss(p, c, w) local d = (p - c) / w; return math.exp(-d * d); end
local function beat(p)
    local y = 0.10 * gauss(p, 0.10, 0.03)          -- P
            - 0.18 * gauss(p, 0.185, 0.012)        -- Q
            + 1.00 * gauss(p, 0.21, 0.016)         -- R
            - 0.55 * gauss(p, 0.24, 0.016);        -- S
    if p > 0.25 and p < 0.62 then                   -- decaying wobble after the spike
        local t = (p - 0.25) / 0.37;
        y = y + 0.45 * math.sin(t * math.pi * 3.0) * (1 - t) ^ 1.4;
    end
    return y;
end

-- Cheap deterministic noise for the Danger jitter.
local function noise(n)
    local x = math.sin(n * 12.9898) * 43758.5453;
    return (x - math.floor(x)) * 2 - 1;
end

local function now()
    return os.clock();
end

local function stateFor(id, w)
    local s = states[id];
    if s == nil or s.w ~= w then
        s = { w = w, head = 0, phase = math.random(), samples = {}, last = now(), flash = 0, n = 0, skip = false };
        states[id] = s;
    end
    s.seen = now();
    return s;
end

-- Advance the write head for this frame, writing samples for each column it passes.
local function advance(s, hpp, w)
    local t = now();
    local dt = math.max(0, math.min(0.1, t - s.last));
    s.last = t;
    local speed = w / SWEEP_SECONDS;                 -- px per second
    local cols = dt * speed;
    local bpm = M.Bpm(hpp);
    local danger = hpp > 0 and hpp < 25;
    local perCol = (bpm / 60) / speed;               -- beat phase per pixel
    local x = s.head;
    local target = s.head + cols;
    while x < target do
        local col = math.floor(x) % w;
        local y = 0;
        if bpm > 0 then
            local before = s.phase;
            local rate = perCol;
            if danger then rate = rate * (1 + 0.25 * noise(s.n * 0.37)); end
            s.phase = s.phase + rate;
            if s.phase >= 1 then
                s.phase = s.phase - 1;
                s.n = s.n + 1;
                -- In Danger, now and then a beat comes out weak and early.
                s.skip = danger and noise(s.n) > 0.55;
            end
            if before < 0.21 and s.phase >= 0.21 then s.flash = t; end
            y = beat(s.phase) * (s.skip and 0.45 or 1);
            if danger then y = y + 0.07 * noise(s.n * 31 + col); end
        end
        s.samples[col] = y;
        x = x + 1;
    end
    s.head = target % w;
end

-- Forget monitors that haven't been drawn for a while (mobs that despawned, etc.).
function M.Prune()
    local t = now();
    for id, s in pairs(states) do
        if t - (s.seen or 0) > 10 then states[id] = nil; end
    end
end

-- Draw the monitor in the rectangle x, y, w, h. Returns the condition color (for labels).
function M.Draw(dl, id, x, y, w, h, hpp, labelFn)
    w, h = math.max(20, math.floor(w)), math.max(10, math.floor(h));
    local s = stateFor(id, w);
    advance(s, hpp, w);
    local color, label = M.Condition(hpp);
    local r, g, b = color[1], color[2], color[3];
    local function col(a) return imgui.GetColorU32({ r, g, b, a }); end

    -- Panel, scanlines, HP strip along the bottom.
    dl:AddRectFilled({ x, y }, { x + w, y + h }, imgui.GetColorU32({ 0.015, 0.045, 0.03, 0.88 }), 3);
    for ly = y + 3, y + h - 3, 4 do
        dl:AddLine({ x + 2, ly }, { x + w - 2, ly }, col(0.07), 1);
    end
    local frac = math.max(0, math.min(1, hpp / 100));
    dl:AddRectFilled({ x + 1, y + h - 3 }, { x + 1 + (w - 2) * frac, y + h - 1 }, col(0.85));

    -- Border, brighter for a moment on each beat.
    local pulse = math.exp(-(now() - s.flash) * 7);
    dl:AddRect({ x, y }, { x + w, y + h }, col(0.35 + 0.55 * pulse), 3, 0, 1 + pulse);

    -- The trace: thick faint pass for glow, then a thin bright pass; older samples fade.
    dl:PushClipRect({ x + 1, y + 1 }, { x + w - 1, y + h - 1 }, true);
    local mid = y + (h - 3) / 2;
    local amp = (h - 8) * 0.42;
    local head = s.head;
    local function point(c) return x + c, mid - (s.samples[c] or 0) * amp; end
    for pass = 1, 2 do
        local thick = pass == 1 and 4 or 1.6;
        local alphaScale = pass == 1 and 0.22 or 1.0;
        for c = 0, w - 1 - STEP, STEP do
            local age = ((head - c) % w) / w;          -- 0 at the head, ~1 just ahead of it
            if age > 0.04 and s.samples[c] and s.samples[c + STEP] then
                local a = (0.18 + 0.82 * (1 - age) ^ 0.9) * alphaScale;
                if a > 0.02 then
                    local x1, y1 = point(c);
                    local x2, y2 = point(c + STEP);
                    dl:AddLine({ x1, y1 }, { x2, y2 }, col(a), thick);
                end
            end
        end
    end
    -- The write head.
    local hc = math.floor(head / STEP) * STEP;
    local hx, hy = point(hc);
    dl:AddCircleFilled({ hx, hy }, 5, col(0.25));
    dl:AddCircleFilled({ hx, hy }, 2.2, imgui.GetColorU32({ math.min(1, r + 0.4), math.min(1, g + 0.4), math.min(1, b + 0.4), 1 }));
    dl:PopClipRect();

    if labelFn then labelFn(label, { r, g, b, 1 }); end
    return { r, g, b, 1 }, label;
end

return M;
