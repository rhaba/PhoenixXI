--[[
    enemybar - Zelda-style heart containers.

    HP is shown as a row of pixel-art hearts (10 by default). Each heart is an equal share of
    HP and empties a half at a time, rounded up, so a mob with any HP left keeps at least half
    a heart. A half that is lost flashes white and fades; while HP is low the last heart throbs,
    like Zelda's low-health warning. When HP goes up (regen, a cure) a wave rolls left to right
    across the hearts, each bobbing up and glinting a moment after the one before; while the mob
    shows a Regen effect a gentle wave repeats every few seconds. Drawn pixel by pixel with filled rectangles, so it needs no
    textures and stays crisp at any size.
]]

local imgui = require('imgui');

local M = {};

-- 11 x 10 pixel heart. o = outline, r = fill, w = highlight, d = shade.
local HEART = {
    '..ooo.ooo..',
    '.orrrorrro.',
    'orwwrrrrrro',
    'orwrrrrrrdo',
    'orrrrrrrrdo',
    '.orrrrrrdo.',
    '..orrrrdo..',
    '...orrdo...',
    '....odo....',
    '.....o.....',
};
local GRID_W, GRID_H = 11, 10;
local MID = 5;  -- columns 0..5 are the left half

local COLORS = {
    o = { 0.10, 0.06, 0.07, 1.0 },
    r = { 0.93, 0.18, 0.22, 1.0 },
    w = { 1.00, 0.72, 0.72, 1.0 },
    d = { 0.66, 0.08, 0.14, 1.0 },
    -- Empty container
    er = { 0.24, 0.24, 0.30, 1.0 },
    ew = { 0.36, 0.36, 0.44, 1.0 },
    ed = { 0.17, 0.17, 0.22, 1.0 },
};

local states = {};

local WAVE_STEP = 0.055;     -- seconds between neighbouring hearts in a wave
local WAVE_WIDTH = 0.09;     -- how long each heart's bump lasts
local REGEN_EVERY = 3.0;     -- repeat interval while a Regen effect is showing

local function now() return os.clock(); end

-- Halves of hearts to show for an HP percent.
function M.Halves(hpp, count)
    if hpp <= 0 then return 0; end
    return math.max(1, math.min(count * 2, math.ceil(hpp / 100 * count * 2 - 1e-9)));
end

-- Size of the heart block for layout: width, height.
function M.Size(count, perRow, size)
    perRow = math.max(1, math.min(count, perRow));
    local rows = math.ceil(count / perRow);
    local px = math.max(1, math.floor(size / GRID_W));
    local hw, hh = GRID_W * px, GRID_H * px;
    local gap = math.max(1, px);
    return perRow * (hw + gap) - gap, rows * (hh + gap) - gap, px;
end

local function stateFor(id, halves)
    local s = states[id];
    if s == nil then
        s = { halves = halves, lost = {}, hpp = nil, wave = nil };
        states[id] = s;
    end
    s.seen = now();
    return s;
end

function M.Prune()
    local t = now();
    for id, s in pairs(states) do
        if t - (s.seen or 0) > 10 then states[id] = nil; end
    end
end

local function u32(c, alpha)
    return imgui.GetColorU32({ c[1], c[2], c[3], (c[4] or 1) * (alpha or 1) });
end

-- Draw one heart. fill: 0 = empty, 1 = left half, 2 = full. flash: 0..1 white flash on the
-- given half ('left'/'right'). scale grows the heart around its center (low-health throb).
local function drawHeart(dl, x, y, px, fill, flashLeft, flashRight, scale, lift, glint)
    scale = scale or 1;
    y = y - (lift or 0);
    glint = glint or 0;
    local p = px * scale;
    local ox = x + (GRID_W * px - GRID_W * p) / 2;
    local oy = y + (GRID_H * px - GRID_H * p) / 2;
    for row = 1, GRID_H do
        local line = HEART[row];
        for col = 0, GRID_W - 1 do
            local ch = line:sub(col + 1, col + 1);
            if ch ~= '.' then
                local left = col <= MID;
                local filled = (fill == 2) or (fill == 1 and left);
                local color;
                if ch == 'o' then
                    color = COLORS.o;
                elseif filled then
                    color = COLORS[ch];
                else
                    color = (ch == 'w' and COLORS.ew) or (ch == 'd' and COLORS.ed) or COLORS.er;
                end
                local px0, py0 = ox + col * p, oy + (row - 1) * p;
                dl:AddRectFilled({ px0, py0 }, { px0 + p, py0 + p }, u32(color));
                local flash = left and flashLeft or flashRight;
                if flash > 0 and ch ~= 'o' then
                    dl:AddRectFilled({ px0, py0 }, { px0 + p, py0 + p }, imgui.GetColorU32({ 1, 1, 1, flash }));
                end
                if glint > 0 and ch ~= 'o' and filled then
                    dl:AddRectFilled({ px0, py0 }, { px0 + p, py0 + p }, imgui.GetColorU32({ 1, 0.92, 0.92, glint }));
                end
            end
        end
    end
end

-- Draw the hearts for a bar. Returns the width and height used.
-- regen: true while the mob shows a Regen effect (repeats the wave every few seconds).
function M.Draw(dl, id, x, y, hpp, count, perRow, size, regen)
    count = math.max(1, count or 10);
    perRow = math.max(1, math.min(count, perRow or 10));
    local w, h, px = M.Size(count, perRow, size or 18);
    local halves = M.Halves(hpp or 0, count);
    local s = stateFor(id, halves);
    local t = now();

    -- Remember each half lost since last frame so it can flash and fade.
    if halves < s.halves then
        for i = halves + 1, s.halves do s.lost[i] = t; end
    elseif halves > s.halves then
        for i = s.halves + 1, halves do s.lost[i] = nil; end
    end
    s.halves = halves;

    -- Healing wave: start one whenever HP goes up, and every few seconds while regen shows.
    hpp = hpp or 0;
    -- lastWave survives the wave ending, so Regen repeats every REGEN_EVERY seconds, not back to back.
    if s.hpp ~= nil and hpp > s.hpp and hpp > 0 then
        if s.lastWave == nil or t - s.lastWave > count * WAVE_STEP * 0.5 then s.wave, s.lastWave = t, t; end
    elseif regen and hpp > 0 and hpp < 100 and (s.lastWave == nil or t - s.lastWave > REGEN_EVERY) then
        s.wave, s.lastWave = t, t;
    end
    s.hpp = hpp;
    if s.wave and t - s.wave > count * WAVE_STEP + WAVE_WIDTH * 4 then s.wave = nil; end

    local hw, hh = GRID_W * px, GRID_H * px;
    local gap = math.max(1, px);
    local low = halves > 0 and halves <= math.max(2, math.ceil(count * 2 * 0.2));
    local lastHeart = math.ceil(halves / 2);
    for i = 1, count do
        local col = (i - 1) % perRow;
        local row = math.floor((i - 1) / perRow);
        local hx, hy = x + col * (hw + gap), y + row * (hh + gap);
        local leftHalf, rightHalf = i * 2 - 1, i * 2;
        local fill = (halves >= rightHalf) and 2 or ((halves >= leftHalf) and 1 or 0);
        local function flash(half)
            local at = s.lost[half];
            if at == nil then return 0; end
            local a = 1 - (t - at) / 0.45;
            if a <= 0 then s.lost[half] = nil; return 0; end
            return a;
        end
        local scale, lift, glint = 1, 0, 0;
        if s.wave then
            local d = (t - s.wave - (i - 1) * WAVE_STEP) / WAVE_WIDTH;
            local bump = math.exp(-d * d);
            if bump > 0.01 then
                scale = 1 + 0.14 * bump;
                lift = px * 2.5 * bump;
                glint = 0.45 * bump;
            end
        end
        if low and i == lastHeart then
            -- Throb: quick double beat, roughly once a second.
            local ph = (t * 1.1) % 1;
            local beat = math.max(math.exp(-((ph - 0.08) / 0.05) ^ 2), 0.7 * math.exp(-((ph - 0.26) / 0.05) ^ 2));
            scale = scale * (1 + 0.18 * beat);
        end
        drawHeart(dl, hx, hy, px, fill, flash(leftHalf), flash(rightHalf), scale, lift, glint);
    end
    return w, h;
end

return M;
