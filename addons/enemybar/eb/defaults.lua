--[[
    enemybar - default settings. Colors are {r, g, b, a} floats.
    Bar positions are each window's top-left corner on screen.
]]

require('common');
local util = require('eb.util');
local rgb = util.rgb255;

local M = {};

-- Fill colors used when a bar is set to color by claim state.
function M.claimPalette()
    return T{
        unclaimed = rgb(200, 185, 60),
        party     = rgb(220, 50, 50),
        alliance  = rgb(220, 90, 170),
        others    = rgb(140, 90, 240),
        member    = rgb(60, 200, 210),
        player    = rgb(200, 200, 200),
        npc       = rgb(110, 190, 110),
        dead      = rgb(110, 110, 110),
    };
end

local function frame(over)
    local f = T{
        show = true,
        x = 650, y = 740,
        width = 600,
        barHeight = 12,
        fontSize = 14,
        showName = true,
        showHpp = true,
        showDistance = false,     -- off: PhoenixXI approves enemybar2 with distance display off
        showAction = true,        -- spell / TP move being readied
        showCastProgress = true,  -- thin progress line under the action text
        showTargetIcon = false,   -- marker when this mob is your target / sub-target
        showInlineTot = false,    -- enemybar2's arrow + target-of-target name beside the bar
        showResists = false,      -- weaknesses / resistances above the bar (needs MobDB installed)
        showImmunities = true,    -- status immunity icons in that row
        resistIconSize = 14,
        showDebuffs = true,
        debuffPosition = 'below', -- 'below' or 'right'
        debuffTimers = true,
        iconSize = 22,
        maxIcons = 12,
        color = rgb(255, 0, 0),
        style = 'farplane9',      -- 'farplane9' (FF9 / Farplane), 'bar' (enemybar2), 'ekg' (Resident Evil) or 'hearts' (Zelda)
        ekgHeight = 34,
        heartCount = 10,          -- 'hearts' style: heart containers, each an equal share of HP
        heartsPerRow = 10,
        heartSize = 22,           -- pixels per heart (pixel art scales in whole steps of 11)
        colorByClaim = false,
        claimColors = M.claimPalette(),
    };
    for k, v in pairs(over or {}) do f[k] = v; end
    return f;
end

M.settings = T{
    theme = 'Farplane9',            -- settings window theme: Phoenix, Farplane, Umbrella, Midnight or Classic
    styleRev = 0,                   -- one-time migrations of saved bar styles
    fontFamily = 'Arial',
    statusIconTheme = 'XIView',     -- or '-Default-' for the game's own icons
    showUncertainMarker = true,     -- yellow "?" on debuffs inferred rather than confirmed
    timerFontSize = 10,
    -- Name / target-of-target text colors by claim state (enemybar2's tints).
    nameColors = T{
        unclaimed = rgb(230, 230, 138),
        party     = rgb(255, 130, 130),
        alliance  = rgb(255, 142, 205),
        others    = rgb(153, 102, 255),
        member    = rgb(102, 255, 255),
        player    = rgb(255, 255, 255),
        npc       = rgb(150, 225, 150),
        dead      = rgb(155, 155, 155),
    },
    frames = T{
        target = frame({ showResists = true }),
        tot = frame({
            x = 1290, y = 740, width = 250, fontSize = 12,
            showAction = false, showDebuffs = false, iconSize = 18, maxIcons = 8,
            color = rgb(0, 150, 160),
        }),
        subtarget = frame({
            x = 680, y = 690, width = 300, fontSize = 12, iconSize = 18, maxIcons = 10,
            color = rgb(12, 50, 101), showTargetIcon = true,
        }),
        focus = frame({
            x = 680, y = 640, width = 250, fontSize = 12, iconSize = 18, maxIcons = 8, showResists = true,
            color = rgb(93, 0, 255),
        }),
        aggro = frame({
            show = false,
            x = 350, y = 550, width = 180, barHeight = 10, fontSize = 10,
            showDistance = false, showCastProgress = false, showTargetIcon = true, showInlineTot = true,
            debuffPosition = 'right', debuffTimers = false, iconSize = 14, maxIcons = 4,
            color = rgb(0, 150, 50),
            count = 6, stackDir = 'down', stackPadding = 30,
        }),
    },
};

M.FRAME_ORDER = { 'target', 'tot', 'subtarget', 'focus', 'aggro' };
M.FRAME_LABELS = {
    target = 'Target', tot = 'Target of Target', subtarget = 'Sub-target', focus = 'Focus', aggro = 'Aggro Stack',
};
M.CLAIM_ORDER = { 'unclaimed', 'party', 'alliance', 'others', 'member', 'player', 'npc', 'dead' };
M.CLAIM_LABELS = {
    unclaimed = 'Unclaimed mob', party = 'Claimed by you / party', alliance = 'Claimed by alliance',
    others = 'Claimed by others', member = 'Party member / pet', player = 'Other player',
    npc = 'NPC', dead = 'Dead',
};
M.FONTS = { 'Arial', 'Tahoma', 'Verdana', 'Segoe UI', 'Calibri', 'Trebuchet MS', 'Consolas' };

return M;
