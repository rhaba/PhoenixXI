--[[
    enemybar for Ashita v4

    Big, separately movable HP bars for your target, target of target, sub-target, a focus
    target and every mob on your party (aggro stack), with buff/debuff icons and timers.

    Ported from enemybar2 for Windower 4 by mmckee and akaden
    (https://github.com/AkadenTK/enemybar2), Copyright (c) 2015, Mike McKee, BSD 3-Clause.
    Buff/debuff tracking, cast tracking and status icons are taken from XIUI by tirem
    (https://github.com/tirem/XIUI), GPL-3.0. This addon is therefore distributed under the
    GNU GPL v3; see LICENSE and NOTICE.md.
]]

addon.name    = 'enemybar';
addon.author  = 'mmckee, akaden (enemybar2); XIUI authors (debuff tracking); Ashita port by Spongeh';
addon.version = '1.5.1';
addon.desc    = 'Movable enemy HP bars with target of target, focus, aggro stack and debuff timers.';
addon.link    = '';

require('common');
local imgui = require('imgui');
local chat = require('chat');
local settingsLib = require('settings');

-- XIUI's handlers read these globals; define them before requiring any of its files.
HzLimitedMode = false;  -- standard (retail/LSB) debuff durations, not HorizonXI's overlay
gConfig = {
    statusIconTheme = 'XIView',
    showUncertainDebuffMarker = true,
    fontFamily = 'Arial',
    fontOutlineWidth = 1,
    globalScale = 1.0,
    targetBarIconFontSize = 10,
};
function GetUIDrawList() return imgui.GetWindowDrawList(); end
function ARGBToImGui(argb)
    return {
        bit.rshift(bit.band(argb, 0xFF0000), 16) / 255,
        bit.rshift(bit.band(argb, 0xFF00), 8) / 255,
        bit.band(argb, 0xFF) / 255,
        bit.rshift(bit.band(argb, 0xFF000000), 24) / 255,
    };
end

local packets = require('libs.packets');
local debuffHandler = require('handlers.debuffhandler');
local enemyCasts = require('handlers.enemycasts');
local actionTracker = require('handlers.actiontracker');
local statusHandler = require('handlers.statushandler');
local TextureManager = require('libs.texturemanager');
local memory = require('libs.memory');
local imtext = require('libs.imtext');

local defaults = require('eb.defaults');
local util = require('eb.util');
local tracker = require('eb.tracker');
local render = require('eb.render');
local configui = require('eb.configui');

local settings = settingsLib.load(defaults.settings);
local ui = require('phxui');
if ui.adoptPackTheme(settings, 'theme') then settingsLib.save(); end
ui.setTheme(settings.theme);

-- One-time: every bar switches to the Farplane IX style (1.5.0).
local function migrate(s)
    local changed = false;
    if (s.styleRev or 0) < 1 then
        for _, key in ipairs(defaults.FRAME_ORDER) do
            if s.frames[key] then s.frames[key].style = 'farplane9'; end
        end
        s.styleRev = 1;
        changed = true;
    end
    -- One-time: distance display off (1.5.1), matching enemybar2's PhoenixXI approval condition.
    if (s.styleRev or 0) < 2 then
        for _, key in ipairs(defaults.FRAME_ORDER) do
            if s.frames[key] then s.frames[key].showDistance = false; end
        end
        s.styleRev = 2;
        changed = true;
    end
    return changed;
end
if migrate(settings) then settingsLib.save(); end

settingsLib.register('settings', 'enemybar_settings_update', function (s)
    if s ~= nil then
        settings = s;
        if migrate(settings) then settingsLib.save(); end
        if ui.adoptPackTheme(settings, 'theme') then settingsLib.save(); end
        ui.setTheme(settings.theme);
    end
end);

local function save()
    settingsLib.save();
end

local function msg(text)
    print(chat.header(addon.name):append(chat.message(text)));
end

local function err(text)
    print(chat.header(addon.name):append(chat.error(text)));
end

local function syncXiuiConfig()
    gConfig.statusIconTheme = settings.statusIconTheme;
    gConfig.showUncertainDebuffMarker = settings.showUncertainMarker;
    gConfig.fontFamily = settings.fontFamily;
    gConfig.targetBarIconFontSize = settings.timerFontSize;
end

-- ---------------------------------------------------------------------------
-- Events
-- ---------------------------------------------------------------------------
ashita.events.register('load', 'enemybar_load', function ()
    -- Load every selectable font now, outside a frame: changing ImGui's font atlas while a
    -- frame is being drawn crashes Ashita 4.16.
    imtext.PrewarmFonts(defaults.FONTS);
    tracker.RefreshParty();
end);

ashita.events.register('unload', 'enemybar_unload', function ()
    save();
    statusHandler.clear_cache();
    TextureManager.clear();
end);

ashita.events.register('packet_in', 'enemybar_packet_in', function (e)
    debuffHandler.HandleIncomingPacket(e);
    if e.id == 0x028 then
        local ap = packets.ParseActionPacket(e);
        if ap then
            enemyCasts.HandleActionPacket(ap);
            debuffHandler.HandleActionPacket(ap);
            actionTracker.HandleActionPacket(ap);
            tracker.HandleActionPacket(ap);
        end
    elseif e.id == 0x029 then
        local m = packets.ParseMessagePacket(e.data);
        if m then
            debuffHandler.HandleMessagePacket(m);
            enemyCasts.HandleMessagePacket(m);
        end
    elseif e.id == 0x00A then
        debuffHandler.HandleZonePacket(e);
        enemyCasts.HandleZonePacket();
        actionTracker.HandleZonePacket();
        tracker.HandleZone();
        require('eb.resists').HandleZone();
        packets.ClearEntityCache();
        statusHandler.clear_zone_cache();
        TextureManager.clearOnZone();
        memory.ResetD3D8Device();
    elseif e.id == 0x00B then
        TextureManager.clearOnZone();
        memory.ResetD3D8Device();
    elseif e.id == 0x0DD then
        tracker.nextPartyRefresh = 0;  -- party changed: rebuild the party cache next frame
    end
end);

ashita.events.register('d3d_present', 'enemybar_present', function ()
    -- Textures evicted last frame are released here, before anything draws.
    TextureManager.FlushPendingReleases();
    syncXiuiConfig();
    tracker.Tick();

    local ok, result = pcall(render.Render, settings);
    if not ok then
        if not render.errorReported then
            render.errorReported = true;
            err('Drawing error: ' .. tostring(result));
        end
    elseif result then
        save();
    end

    local okUi, uiChanged = pcall(configui.Render, settings);
    if okUi and uiChanged then save(); end
    if not okUi and not configui.errorReported then
        configui.errorReported = true;
        err('Settings window error: ' .. tostring(uiChanged));
    end

    statusHandler.FlushTooltip();
end);

-- ---------------------------------------------------------------------------
-- Commands: /eb or /enemybar
-- ---------------------------------------------------------------------------
local function setFocus(args)
    local arg = table.concat(args, ' ', 3);
    if arg == '' then
        local t = AshitaCore:GetMemoryManager():GetTarget();
        local index = t:GetTargetIndex(t:GetIsSubTargetActive() == 1 and 1 or 0);
        local e = util.entity(index);
        if e == nil then err('Target something first, or give a name: /eb ft <name>'); return; end
        tracker.focusId = e.ServerId;
        msg('Focus target: ' .. e.Name);
    elseif arg:lower() == 'clear' or arg:lower() == 'off' then
        tracker.focusId = nil;
        msg('Focus target cleared.');
    elseif tonumber(arg) then
        tracker.focusId = tonumber(arg);
        msg('Focus target id: ' .. arg);
    else
        local index, e = tracker.FindByName(arg);
        if e == nil then err('No one nearby matches "' .. arg .. '".'); return; end
        tracker.focusId = e.ServerId;
        msg('Focus target: ' .. e.Name);
    end
end

local HELP = {
    '/eb - open or close the settings window',
    '/eb setup - toggle setup mode: demo bars you can drag (Ctrl snaps to a 10px grid)',
    '/eb ft [name|id|clear] - set the focus target (no argument = your current target)',
    '/eb show|hide <target|tot|subtarget|focus|aggro> - turn a bar on or off',
    '/eb reset - move every bar back to its default spot',
    '/eb theme <name> - settings window theme: Phoenix, Farplane, Umbrella, Midnight or Classic',
};

local FRAME_ALIASES = {
    t = 'target', target = 'target',
    tot = 'tot', tt = 'tot',
    st = 'subtarget', subtarget = 'subtarget',
    f = 'focus', ft = 'focus', focus = 'focus', focustarget = 'focus',
    a = 'aggro', aggro = 'aggro',
};

ashita.events.register('command', 'enemybar_command', function (e)
    local args = e.command:args();
    if #args == 0 then return; end
    local cmd = args[1]:lower();
    if cmd ~= '/eb' and cmd ~= '/enemybar' then return; end
    e.blocked = true;

    local sub = (args[2] or ''):lower();
    if sub == '' or sub == 'config' then
        configui.open[1] = not configui.open[1];
    elseif sub == 'setup' or sub == 'demo' or sub == 'test' then
        local want = (args[3] or ''):lower();
        if want == 'on' then render.setup = true;
        elseif want == 'off' then render.setup = false;
        else render.setup = not render.setup; end
        msg('Setup mode ' .. (render.setup and 'on: drag the bars where you want them.' or 'off.'));
    elseif sub == 'ft' or sub == 'focus' or sub == 'focustarget' or sub == 'f' then
        setFocus(args);
    elseif sub == 'show' or sub == 'hide' then
        local key = FRAME_ALIASES[(args[3] or ''):lower()];
        if key == nil then err('Usage: /eb ' .. sub .. ' <target|tot|subtarget|focus|aggro>'); return; end
        settings.frames[key].show = (sub == 'show');
        save();
        msg(defaults.FRAME_LABELS[key] .. ' bar ' .. (sub == 'show' and 'shown.' or 'hidden.'));
    elseif sub == 'theme' then
        local name = ui.findTheme(args[3]);
        if name == nil then
            msg('Themes: ' .. table.concat(ui.THEMES, ', ') .. '. Use /eb theme <name>.');
        else
            settings.theme = ui.setTheme(name);
            save();
            msg('Theme: ' .. name);
        end
    elseif sub == 'reset' then
        for _, key in ipairs(defaults.FRAME_ORDER) do
            local d = defaults.settings.frames[key];
            settings.frames[key].x, settings.frames[key].y = d.x, d.y;
        end
        render.ResetPositions();
        save();
        msg('All bars moved back to their default positions.');
    else
        for _, line in ipairs(HELP) do msg(line); end
    end
end);
