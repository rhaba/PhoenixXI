--[[
    npcgil-phx for Ashita v4 - PhoenixXI

    Tallies the items you win from treasure pools and the gil you loot from kills during a farming
    session, valued at what the best-paying NPC gives you for your fame, and flags items a quest
    takes as a turn-in.
]]

addon.name    = 'npcgil_phx';
addon.author  = 'Spongeh';
addon.version = '2.3.0';
addon.desc    = 'Farming session tracker for PhoenixXI: treasure-pool loot at fame-adjusted NPC prices, quest turn-ins, plus gil looted from kills.';
addon.link    = '';

require('common');
local chat    = require('chat');
local session = require('session');

local reported = false;

ashita.events.register('d3d_present', 'npcgil_phx_present', function ()
    local ok, err = pcall(session.Tick);
    if not ok and not reported then
        reported = true;
        print(chat.header(addon.name):append(chat.error('Error: ' .. tostring(err))));
    end
end);

ashita.events.register('unload', 'npcgil_phx_unload', function ()
    session.Save();
end);

ashita.events.register('command', 'npcgil_phx_command', function (e)
    local args = e.command:args();
    if (#args == 0) then return; end
    local cmd = args[1]:lower();
    if (cmd ~= '/npcgil') and (cmd ~= '/ngp') then return; end
    e.blocked = true;

    local sub = (args[2] or ''):lower();
    if (sub == '') then
        session.Toggle();
    elseif (sub == 'show') or (sub == 'hide') then
        session.Toggle(sub == 'show');
    elseif (sub == 'reset') then
        session.Reset();
    elseif (sub == 'report') then
        session.Report();
    elseif (sub == 'theme') then
        local ui = require('phxui');
        local name = ui.findTheme(args[3]);
        if (name == nil) then
            print(chat.header(addon.name):append(chat.message('Themes: ' .. table.concat(ui.THEMES, ', ') .. '. Use /npcgil theme <name>, or right-click the window.')));
        else
            session.SetTheme(name);
            print(chat.header(addon.name):append(chat.message('Theme: ' .. name)));
        end
    else
        print(chat.header(addon.name):append(chat.message('/npcgil [show|hide|reset|report|theme <name>] (or /ngp). Right-click the window to change the theme.')));
    end
end);
