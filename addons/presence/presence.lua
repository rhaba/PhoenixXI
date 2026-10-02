addon.name    = 'presence';
addon.author  = 'Spongeh';
addon.version = '2.0.0';
addon.desc    = 'Shows your FFXI job and zone as your Discord Rich Presence ("Playing ...") status.';
addon.link    = '';

require('common');
local settings = require('settings');
local chat     = require('chat');
local ffi      = require('ffi');

--[[
    Talks to the local Discord desktop client over its named-pipe IPC
    protocol (the same one the Discord Game SDK / discord-rpc use) to set
    this user's Rich Presence. Nothing is posted to any channel - this only
    changes the "Playing ..." line under the local Discord account.

    Requires a free Discord "Application" (Client ID only, no bot) created
    at https://discord.com/developers/applications - see README.md.
]]
ffi.cdef[[
    typedef void* HANDLE;

    HANDLE CreateFileA(const char* lpFileName, uint32_t dwDesiredAccess, uint32_t dwShareMode, void* lpSecurityAttributes, uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes, HANDLE hTemplateFile);
    int WriteFile(HANDLE hFile, const char* lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t* lpNumberOfBytesWritten, void* lpOverlapped);
    int ReadFile(HANDLE hFile, void* lpBuffer, uint32_t nNumberOfBytesToRead, uint32_t* lpNumberOfBytesRead, void* lpOverlapped);
    int PeekNamedPipe(HANDLE hNamedPipe, void* lpBuffer, uint32_t nBufferSize, uint32_t* lpBytesRead, uint32_t* lpTotalBytesAvail, uint32_t* lpBytesLeftThisMessage);
    int CloseHandle(HANDLE hObject);
    uint32_t GetCurrentProcessId();
]]

local kernel32 = ffi.load('kernel32');

local GENERIC_READ         = 0x80000000;
local GENERIC_WRITE        = 0x40000000;
local OPEN_EXISTING        = 3;
local INVALID_HANDLE_VALUE = ffi.cast('HANDLE', -1);

-- Discord IPC opcodes.
local OP_HANDSHAKE = 0;
local OP_FRAME     = 1;

-- Job id -> abbreviation, index 0 is NON. Stable since Seekers of Adoulin (RUN added last).
local job_names = {
    [0] = 'NON', 'WAR', 'MNK', 'WHM', 'BLM', 'RDM', 'THF', 'PLD', 'DRK', 'BST',
    'BRD', 'RNG', 'SAM', 'NIN', 'DRG', 'SMN', 'BLU', 'COR', 'PUP', 'DNC',
    'SCH', 'GEO', 'RUN',
};

local default_settings = T{
    client_id     = '',
    enabled       = true,
    show_job      = true,
    show_zone     = true,
    large_image   = '',
    large_text    = '',
    poll_interval = 2.0,
};

local presence = {
    settings             = settings.load(default_settings),
    discord_handle       = nil,
    next_connect_attempt = 0,
    discord_dirty        = false,
    last_poll            = 0,
    last_active          = false,
    last_name            = nil,
    session_start        = nil,
    last_main_job        = nil,
    last_sub_job         = nil,
    last_main_lvl        = nil,
    last_sub_lvl         = nil,
    last_zone            = nil,
};

math.randomseed(os.time());

local function job_abbr(id)
    return job_names[id] or ('Job ' .. tostring(id));
end

local function get_zone_name(zone_id)
    local ok, name = pcall(function ()
        return AshitaCore:GetResourceManager():GetString('zones.names', zone_id);
    end);
    if (ok and name ~= nil and name ~= '') then
        return name;
    end
    return ('Zone %d'):format(zone_id or -1);
end

local function json_escape(str)
    str = tostring(str or '');
    str = str:gsub('\\', '\\\\');
    str = str:gsub('"', '\\"');
    str = str:gsub('\n', '\\n');
    str = str:gsub('\r', '\\r');
    str = str:gsub('\t', '\\t');
    return str;
end

--[[
    IPC pipe helpers
]]

local function pipe_write_frame(handle, opcode, payload)
    local header = ffi.new('int32_t[2]', opcode, #payload);
    local frame = ffi.string(header, 8) .. payload;
    local written = ffi.new('uint32_t[1]');
    return kernel32.WriteFile(handle, frame, #frame, written, nil) ~= 0;
end

-- Non-blocking: discards any pending reply bytes so the OS pipe buffer
-- doesn't build up over a long session. We don't need the reply's content.
local function discord_drain()
    if (presence.discord_handle == nil) then
        return;
    end

    local avail = ffi.new('uint32_t[1]', 0);
    local peek_ok = kernel32.PeekNamedPipe(presence.discord_handle, nil, 0, nil, avail, nil);
    if (peek_ok == 0) then
        kernel32.CloseHandle(presence.discord_handle);
        presence.discord_handle = nil;
        return;
    end

    if (avail[0] > 0) then
        local buf = ffi.new('char[?]', avail[0]);
        local read_n = ffi.new('uint32_t[1]');
        kernel32.ReadFile(presence.discord_handle, buf, avail[0], read_n, nil);
    end
end

local function discord_connect(client_id)
    for i = 0, 9 do
        local name = ('\\\\.\\pipe\\discord-ipc-%d'):format(i);
        local handle = kernel32.CreateFileA(name, GENERIC_READ + GENERIC_WRITE, 0, nil, OPEN_EXISTING, 0, nil);

        if (handle ~= nil and handle ~= INVALID_HANDLE_VALUE) then
            local hello = ('{"v":1,"client_id":"%s"}'):format(json_escape(client_id));
            if (pipe_write_frame(handle, OP_HANDSHAKE, hello)) then
                return handle;
            end
            kernel32.CloseHandle(handle);
        end
    end
    return nil;
end

-- Retries at most every 15s so a missing/closed Discord client doesn't
-- churn through CreateFileA on every poll tick.
local function ensure_connected()
    if (presence.discord_handle ~= nil) then
        return true;
    end
    if (presence.settings.client_id == nil or presence.settings.client_id == '') then
        return false;
    end

    local now = os.clock();
    if (now < presence.next_connect_attempt) then
        return false;
    end
    presence.next_connect_attempt = now + 15;

    presence.discord_handle = discord_connect(presence.settings.client_id);
    return presence.discord_handle ~= nil;
end

local function build_activity_json(pid, details, state, start_ts)
    local state_json = '';
    if (state ~= nil and state ~= '') then
        state_json = (',"state":"%s"'):format(json_escape(state));
    end

    local assets_json = '';
    if (presence.settings.large_image ~= nil and presence.settings.large_image ~= '') then
        assets_json = (',"assets":{"large_image":"%s","large_text":"%s"}'):format(
            json_escape(presence.settings.large_image),
            json_escape(presence.settings.large_text or ''));
    end

    local activity = ('{"details":"%s"%s,"timestamps":{"start":%d}%s}'):format(
        json_escape(details), state_json, start_ts, assets_json);

    local nonce = ('%d-%d'):format(os.time(), math.random(1000, 9999));

    return ('{"cmd":"SET_ACTIVITY","args":{"pid":%d,"activity":%s},"nonce":"%s"}'):format(pid, activity, nonce);
end

local function build_clear_json(pid)
    local nonce = ('%d-%d'):format(os.time(), math.random(1000, 9999));
    return ('{"cmd":"SET_ACTIVITY","args":{"pid":%d,"activity":null},"nonce":"%s"}'):format(pid, nonce);
end

local function discord_set_activity(details, state, start_ts)
    if (not ensure_connected()) then
        return false;
    end

    local payload = build_activity_json(tonumber(kernel32.GetCurrentProcessId()), details, state, start_ts);
    local ok = pipe_write_frame(presence.discord_handle, OP_FRAME, payload);

    if (not ok) then
        kernel32.CloseHandle(presence.discord_handle);
        presence.discord_handle = nil;
    end
    return ok;
end

local function discord_clear_activity()
    if (presence.discord_handle == nil) then
        return;
    end

    local payload = build_clear_json(tonumber(kernel32.GetCurrentProcessId()));
    local ok = pipe_write_frame(presence.discord_handle, OP_FRAME, payload);

    if (not ok) then
        kernel32.CloseHandle(presence.discord_handle);
        presence.discord_handle = nil;
    end
end

--[[
    Presence polling
]]

local function build_details()
    if (not presence.settings.show_job) then
        return 'Playing FFXI';
    end

    local player = AshitaCore:GetMemoryManager():GetPlayer();
    local main_job, main_lvl = player:GetMainJob(), player:GetMainJobLevel();
    local sub_job, sub_lvl = player:GetSubJob(), player:GetSubJobLevel();

    local details = ('%s%d'):format(job_abbr(main_job), main_lvl);
    if (sub_job ~= nil and sub_job > 0) then
        details = details .. (' / %s%d'):format(job_abbr(sub_job), sub_lvl);
    end
    return details;
end

local function build_state()
    if (not presence.settings.show_zone) then
        return nil;
    end
    local party = AshitaCore:GetMemoryManager():GetParty();
    return get_zone_name(party:GetMemberZone(0));
end

local function check_presence()
    local memMgr = AshitaCore:GetMemoryManager();
    local party  = memMgr:GetParty();
    local player = memMgr:GetPlayer();
    if (party == nil or player == nil) then
        return;
    end

    discord_drain();

    local is_active = party:GetMemberIsActive(0) == 1;
    local name = party:GetMemberName(0);
    local was_active = presence.last_active;

    if (is_active and not was_active) then
        presence.last_active   = true;
        presence.session_start = os.time();
        presence.discord_dirty = true;

        presence.last_main_job = player:GetMainJob();
        presence.last_sub_job  = player:GetSubJob();
        presence.last_main_lvl = player:GetMainJobLevel();
        presence.last_sub_lvl  = player:GetSubJobLevel();
        presence.last_zone     = party:GetMemberZone(0);
    elseif (not is_active and was_active) then
        presence.last_active = false;
        discord_clear_activity();
    end

    if (is_active and name ~= nil and name ~= '') then
        presence.last_name = name;
    end

    if (not is_active or not presence.settings.enabled) then
        return;
    end

    local main_job = player:GetMainJob();
    local sub_job  = player:GetSubJob();
    local main_lvl = player:GetMainJobLevel();
    local sub_lvl  = player:GetSubJobLevel();
    local zone_id  = party:GetMemberZone(0);

    if (main_job ~= presence.last_main_job or sub_job ~= presence.last_sub_job or
        main_lvl ~= presence.last_main_lvl or sub_lvl ~= presence.last_sub_lvl or
        zone_id ~= presence.last_zone) then
        presence.last_main_job = main_job;
        presence.last_sub_job  = sub_job;
        presence.last_main_lvl = main_lvl;
        presence.last_sub_lvl  = sub_lvl;
        presence.last_zone     = zone_id;
        presence.discord_dirty = true;
    end

    if (presence.discord_dirty) then
        local ok = discord_set_activity(build_details(), build_state(), presence.session_start);
        presence.discord_dirty = not ok;
    end
end

local function print_help()
    print(chat.header(addon.name):append(chat.message('Commands:')));
    print(chat.header(addon.name):append(chat.message('/presence clientid <id> - set the Discord application Client ID')));
    print(chat.header(addon.name):append(chat.message('/presence on|off        - enable/disable Rich Presence updates')));
    print(chat.header(addon.name):append(chat.message('/presence job <on|off>  - show job/subjob in the details line')));
    print(chat.header(addon.name):append(chat.message('/presence zone <on|off> - show current zone in the state line')));
    print(chat.header(addon.name):append(chat.message('/presence icon <key> [hover text] - set the large image asset key')));
    print(chat.header(addon.name):append(chat.message('/presence icon clear    - remove the large image asset')));
    print(chat.header(addon.name):append(chat.message('/presence test          - force an immediate presence update')));
    print(chat.header(addon.name):append(chat.message('/presence status        - show current settings/connection state')));
end

ashita.events.register('load', 'load_cb', function ()
    print(chat.header(addon.name):append(chat.message(('v%s loaded. Use /presence help for commands.'):format(addon.version))));
    if (presence.settings.client_id == nil or presence.settings.client_id == '') then
        print(chat.header(addon.name):append(chat.warning('No Discord Client ID set yet - run /presence clientid <id>. See README.md.')));
    end
end);

ashita.events.register('unload', 'unload_cb', function ()
    discord_clear_activity();
    if (presence.discord_handle ~= nil) then
        kernel32.CloseHandle(presence.discord_handle);
        presence.discord_handle = nil;
    end
    settings.save();
end);

ashita.events.register('d3d_present', 'present_cb', function ()
    local now = os.clock();
    if (now - presence.last_poll < presence.settings.poll_interval) then
        return;
    end
    presence.last_poll = now;

    local ok, err = pcall(check_presence);
    if (not ok) then
        print(chat.header(addon.name):append(chat.error('presence check error: ' .. tostring(err))));
    end
end);

ashita.events.register('command', 'command_cb', function (e)
    local args = e.command:args();
    if (#args == 0 or args[1]:lower() ~= '/presence') then
        return;
    end
    e.blocked = true;

    local sub = args[2] and args[2]:lower() or 'help';

    if (sub == 'clientid' and args[3] ~= nil) then
        presence.settings.client_id = args[3];
        if (presence.discord_handle ~= nil) then
            kernel32.CloseHandle(presence.discord_handle);
            presence.discord_handle = nil;
        end
        presence.discord_dirty = true;
        settings.save();
        print(chat.header(addon.name):append(chat.success('Client ID updated.')));
    elseif (sub == 'on') then
        presence.settings.enabled = true;
        presence.discord_dirty = true;
        settings.save();
        print(chat.header(addon.name):append(chat.message('Rich Presence enabled.')));
    elseif (sub == 'off') then
        presence.settings.enabled = false;
        discord_clear_activity();
        settings.save();
        print(chat.header(addon.name):append(chat.message('Rich Presence disabled.')));
    elseif (sub == 'job' and args[3] ~= nil) then
        presence.settings.show_job = args[3]:lower() == 'on';
        presence.discord_dirty = true;
        settings.save();
        print(chat.header(addon.name):append(chat.message('Show job: ' .. (presence.settings.show_job and 'on' or 'off'))));
    elseif (sub == 'icon' and args[3] ~= nil) then
        if (args[3]:lower() == 'clear') then
            presence.settings.large_image = '';
            presence.settings.large_text = '';
            print(chat.header(addon.name):append(chat.message('Large image asset cleared.')));
        else
            local hover_words = {};
            for i = 4, #args do
                table.insert(hover_words, args[i]);
            end

            presence.settings.large_image = args[3];
            presence.settings.large_text = table.concat(hover_words, ' ');
            print(chat.header(addon.name):append(chat.success('Icon set to asset key "' .. args[3] .. '".')));
        end
        presence.discord_dirty = true;
        settings.save();
    elseif (sub == 'zone' and args[3] ~= nil) then
        presence.settings.show_zone = args[3]:lower() == 'on';
        presence.discord_dirty = true;
        settings.save();
        print(chat.header(addon.name):append(chat.message('Show zone: ' .. (presence.settings.show_zone and 'on' or 'off'))));
    elseif (sub == 'test') then
        local ok = discord_set_activity(build_details(), build_state(), presence.session_start or os.time());
        if (ok) then
            print(chat.header(addon.name):append(chat.success('Presence update sent.')));
        else
            print(chat.header(addon.name):append(chat.error('Could not reach Discord (client not running, or Client ID not set).')));
        end
    elseif (sub == 'status') then
        print(chat.header(addon.name):append(chat.message('client id: ' .. (presence.settings.client_id ~= '' and 'set' or 'not set'))));
        print(chat.header(addon.name):append(chat.message('connected: ' .. (presence.discord_handle ~= nil and 'yes' or 'no'))));
        print(chat.header(addon.name):append(chat.message('enabled: ' .. (presence.settings.enabled and 'on' or 'off'))));
        print(chat.header(addon.name):append(chat.message('show job: ' .. (presence.settings.show_job and 'on' or 'off'))));
        print(chat.header(addon.name):append(chat.message('show zone: ' .. (presence.settings.show_zone and 'on' or 'off'))));
        print(chat.header(addon.name):append(chat.message('icon: ' .. (presence.settings.large_image ~= '' and presence.settings.large_image or 'not set'))));
    else
        print_help();
    end
end);

settings.register('settings', 'settings_update', function (s)
    if (s ~= nil) then
        presence.settings = s;
    end
    settings.save();
end);
