--[[
    enemybar - "Farplane IX" bar style: Final Fantasy IX's battle window in Farplane colors,
    matching XivParty's farplane9 layout.

    A smoky charcoal plate with a misty border and an ember hairline on top; the name on the left
    (in its claim color), HP% on the right (ivory, then amber / orange / red as HP drops), and a
    framed gauge underneath that shifts green -> amber -> orange -> red. When HP drops, a pale
    trail shows the HP just lost and catches up after a moment, like FF9's damage gauge.
]]

local imgui = require('imgui');
local ui = require('phxui');

local M = {};

local GREEN  = { 0.56, 0.84, 0.63, 1 };   -- #8FD6A0
local AMBER  = { 1.00, 0.82, 0.48, 1 };   -- #FFD27A
local ORANGE = { 1.00, 0.63, 0.29, 1 };   -- #FFA04A
local RED    = { 1.00, 0.35, 0.23, 1 };   -- #FF5A3A
local IVORY  = { 1.00, 0.96, 0.91, 1 };

local trails = {};

local function u32(c, a) return imgui.GetColorU32({ c[1], c[2], c[3], (c[4] or 1) * (a or 1) }); end

function M.HpColor(hpp)
    if hpp < 25 then return RED; end
    if hpp < 50 then return ORANGE; end
    if hpp < 75 then return AMBER; end
    return GREEN;
end

function M.TextColor(hpp)
    if hpp < 25 then return RED; end
    if hpp < 50 then return ORANGE; end
    if hpp < 75 then return AMBER; end
    return IVORY;
end

-- Height of the plate for a font size.
function M.Height(fontSize)
    return math.floor(fontSize + 15);
end

function M.Prune()
    local t = os.clock();
    for id, tr in pairs(trails) do
        if t - tr.seen > 10 then trails[id] = nil; end
    end
end

-- Draw the plate at x, y (w x h). text(dl, s, x, y, color, size, align) draws outlined text.
function M.Draw(dl, id, x, y, w, h, hpp, name, nameColor, fontSize, text)
    local c = ui.color;
    local t = os.clock();
    hpp = math.max(0, math.min(100, hpp or 0));

    -- trail: jumps up with HP, lingers 0.4s after a drop, then slides down to the new value
    local tr = trails[id];
    if tr == nil then tr = { value = hpp, hold = 0 }; trails[id] = tr; end
    tr.seen = t;
    if hpp >= tr.value then
        tr.value, tr.hold = hpp, 0;
    elseif tr.hold == 0 then
        tr.hold = t + 0.4;
    elseif t > tr.hold then
        tr.value = math.max(hpp, tr.value - (tr.value - hpp) * 0.12 - 0.15);
        if tr.value <= hpp + 0.05 then tr.value, tr.hold = hpp, 0; end
    end

    -- plate
    dl:AddRectFilled({ x, y }, { x + w, y + h }, u32(c.abyss, 0.88), 4);
    dl:AddRect({ x, y }, { x + w, y + h }, u32(c.border, 1.6), 4, 0, 1);
    dl:AddLine({ x + 4, y + 0.5 }, { x + w - 4, y + 0.5 }, u32(c.accent, 0.9), 1);

    -- name and HP%
    local pad = 6;
    text(dl, name or '', x + pad, y + 2, nameColor or IVORY, fontSize, 'left');
    text(dl, string.format('%d%%', hpp), x + w - pad, y + 2, M.TextColor(hpp), fontSize, 'right');

    -- gauge
    local gx0, gx1 = x + pad, x + w - pad;
    local gy0, gy1 = y + h - 10, y + h - 4;
    dl:AddRectFilled({ gx0, gy0 }, { gx1, gy1 }, u32({ 0.043, 0.039, 0.051, 1 }, 0.92));
    local gw = gx1 - gx0 - 2;
    local fillTo = gx0 + 1 + gw * hpp / 100;
    if tr.value > hpp then
        dl:AddRectFilled({ fillTo, gy0 + 1 }, { gx0 + 1 + gw * tr.value / 100, gy1 - 1 }, u32(IVORY, 0.55));
    end
    if hpp > 0 then
        local col = M.HpColor(hpp);
        dl:AddRectFilled({ gx0 + 1, gy0 + 1 }, { fillTo, gy1 - 1 }, u32(col, 0.95));
        dl:AddLine({ gx0 + 1, gy0 + 1.5 }, { fillTo, gy0 + 1.5 }, u32({ 1, 1, 1, 1 }, 0.35), 1);
    end
    dl:AddRect({ gx0, gy0 }, { gx1, gy1 }, u32(c.border, 2.2), 0, 0, 1);
end

return M;
