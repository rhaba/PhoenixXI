--[[
    questtracker for Ashita v4 - PhoenixXI

    Tracks your quests the way WoW does: quests you accept are picked up from the server's quest
    log and tracked automatically, an on-screen list shows each tracked quest's next step, and
    finished steps disappear. /qt opens the journal, where you choose what to track.

    Steps are generated from PhoenixXI's own quest scripts (tools/build_quests.py, data/quests.lua)
    with hand-written wording for key quests (data/overrides.lua). Display only: the addon reads the
    quest log, event and key item information the client already receives and sends nothing.
]]

addon.name    = 'questtracker';
addon.author  = 'Spongeh';
addon.version = '0.3.1';
addon.desc    = 'WoW-style quest tracker: auto-tracks accepted quests and shows the next step on screen.';
addon.link    = '';

require('common');
local chat = require('chat');
local settings = require('settings');
local ui = require('phxui');
local E = require('qt.engine');
local view = require('qt.ui');

local defaults = T{
    theme = 'Farplane9',
    autoTrack = true,
    overlay = true,
    showNext = false,
    locked = false,
    width = 330,
    panelAlpha = 0.6,   -- tracker background opacity (0 = none)
    settingsRev = 0,    -- one-time migrations of saved settings
    maxQuests = 8,
    x = -1, y = -1,
    tracked = T{},      -- [quest id] = true
    step = T{},         -- [quest id] = current step (1-based)
    active = T{},       -- [quest id] = true: current quests, from the server's quest log
    completed = T{},    -- [quest id] = true
    seenLogs = T{},     -- [log] = true once its quest log has arrived (first arrival doesn't auto-track)
    folded = T{},       -- [quest id] = true: folded on the tracker
    minimized = false,  -- tracker folded down to its header
    review = T{},       -- [quest id] = true: found already in progress; tick the steps you've done
    kills = T{},        -- [quest id] = kills counted for the current step's kill counter
};

local cfg = settings.load(defaults);
local function bind(s)
    cfg = s;
    E.cfg = cfg;
    if ui.adoptPackTheme(cfg, 'theme') then settings.save(); end
    ui.setTheme(cfg.theme);
    if (cfg.settingsRev or 0) < 1 then
        -- 0.2.3: the tracker gets a background by default (it was off)
        if (cfg.panelAlpha or 0) <= 0 then cfg.panelAlpha = 0.6; end
        cfg.settingsRev = 1;
        settings.save();
    end
end
bind(cfg);
settings.register('settings', 'questtracker_settings_update', function (s) if s ~= nil then bind(s); end end);

local function msg(text)
    print(chat.header(addon.name):append(chat.message(text)));
end
E.onNotify = msg;

local nextKi, lastSave = 0, 0

ashita.events.register('packet_in', 'questtracker_packet', function (e)
    if e.id == 0x056 or e.id == 0x032 or e.id == 0x034 or e.id == 0x028 or e.id == 0x029 then
        local ok, err = pcall(E.HandlePacket, e);
        if not ok then msg('Error: ' .. tostring(err)); end
    end
end);

ashita.events.register('d3d_present', 'questtracker_present', function ()
    local now = os.clock();
    if now >= nextKi then
        nextKi = now + 2;
        pcall(E.CheckKeyItems);
    end
    local ok, err = pcall(view.DrawTracker, cfg);
    if not ok then msg('Tracker error: ' .. tostring(err)); cfg.overlay = false; end
    ok, err = pcall(view.DrawJournal, cfg, function (name) cfg.theme = ui.setTheme(name); cfg.dirty = true; end);
    if not ok then msg('Journal error: ' .. tostring(err)); view.journalOpen[1] = false; end
    if cfg.dirty and now - lastSave > 2 then
        cfg.dirty = nil;
        lastSave = now;
        settings.save();
    end
end);

ashita.events.register('unload', 'questtracker_unload', function ()
    cfg.dirty = nil;
    settings.save();
end);

-- Find an active quest by (part of) its name.
local function findActive(text)
    local needle = (text or ''):lower();
    if needle == '' then return nil; end
    for _, id in ipairs(E.activeList()) do
        if E.quest(id).name:lower():find(needle, 1, true) then return id; end
    end
end

ashita.events.register('command', 'questtracker_command', function (e)
    local args = e.command:args();
    if #args == 0 then return; end
    local cmd = args[1]:lower();
    if cmd ~= '/qt' and cmd ~= '/questtracker' then return; end
    e.blocked = true;
    local sub = (args[2] or ''):lower();
    local rest = e.command:match('^%S+%s+%S+%s+(.+)$');
    if sub == '' or sub == 'journal' then
        view.journalOpen[1] = not view.journalOpen[1];
    elseif sub == 'show' or sub == 'hide' then
        cfg.overlay = (sub == 'show'); settings.save();
    elseif sub == 'track' or sub == 'untrack' then
        local id = findActive(rest);
        if id then E.setTracked(id, sub == 'track'); msg((sub == 'track' and 'Tracking ' or 'Stopped tracking ') .. E.quest(id).name .. '.');
        else msg('No active quest matching "' .. tostring(rest) .. '".'); end
    elseif sub == 'done' or sub == 'undo' then
        local id = findActive(rest) or E.trackedList()[1];
        if id then E.setStep(id, E.step(id) + (sub == 'done' and 1 or -1), sub == 'undo'); end
    elseif sub == 'auto' then
        cfg.autoTrack = not cfg.autoTrack; settings.save();
        msg('Track new quests automatically: ' .. (cfg.autoTrack and 'on.' or 'off.'));
    elseif sub == 'min' or sub == 'minimize' then
        cfg.minimized = not cfg.minimized or nil; settings.save();
    elseif sub == 'lock' then
        cfg.locked = not cfg.locked; settings.save();
        msg('Tracker ' .. (cfg.locked and 'locked.' or 'unlocked (drag it).'));
    elseif sub == 'theme' then
        local name = ui.findTheme(args[3]);
        if name then cfg.theme = ui.setTheme(name); settings.save(); msg('Theme: ' .. name); end
    else
        msg('/qt - journal   /qt show|hide - tracker   /qt track|untrack <quest>   /qt done|undo [quest] - step done / back');
        msg('/qt min - minimize/restore the tracker   /qt auto - auto-track new quests on/off   /qt lock - lock the tracker   /qt theme <name>');
    end
end);

ashita.events.register('load', 'questtracker_load', function ()
    local n = 0;
    for _ in pairs(cfg.active) do n = n + 1; end
    msg(string.format('v%s loaded: %d active quest%s known. /qt opens the journal.', addon.version, n, n == 1 and '' or 's'));
end);
