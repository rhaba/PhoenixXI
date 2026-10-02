--[[
    npcgil-phx: the loot session (also in VanaCompass - Phoenix as its Loot Session window).

    Tallies the items you win from treasure pools and what the best-paying NPC would give you for
    them at your fame (data/prices.lua holds base prices, built by tools/build_prices.py; fame.lua
    applies PhoenixXI's price ranks). The server announces each pool
    item, then who got it (LandSandBoat src/map/packets/s2c). Offsets are 1-indexed into e.data:

        0x0D2 item added to the pool
            0x11 item id (u16)     0x15 pool slot (u8)
        0x0D3 lot result
            0x05 winner server id (u32)     0x15 pool slot (u8)
            0x16 result (u8): 1 = won, 2 = won but inventory full, 3 = lost / nobody won,
                              0 = a lot was cast, not decided yet

    Only a result of 1 with our own id counts, so items bought, traded, stolen or moved between
    bags never do.

    Gil dropped by mobs (beastmen, mostly) is the "<name> obtains <n> gil." chat line, which is
    battle message 0x029 with message 565. The server sends it only when it hands out gil from a
    kill (charutils::DistributeGil), one message per party member with that member's share:

        0x029 battle message
            0x09 target server id (u32)     0x0D amount (u32)     0x19 message number (u16)

    Items that a quest takes as a turn-in get a check in the Quest column; hovering it lists the
    quests (data/quest_items.lua, built by tools/build_quest_items.py from the server's quest and
    NPC scripts): who takes them, how many, the gil reward, and whether it can be repeated.

    The session and your fame levels are saved per character in their own settings file
    (session). Starting a new session keeps the fame levels.
]]

local settings = require('settings');
local chat     = require('chat');
local imgui    = require('imgui');
local prices   = require('data.prices');
local fame     = require('fame');
local okQuests, questItems = pcall(require, 'data.quest_items');
if not okQuests or type(questItems) ~= 'table' then questItems = {}; end
local ui       = require('phxui');

local ALIAS = 'session';

local default_session = T{
    open    = true,
    elapsed = 0,      -- seconds of logged-in time in this session
    items   = T{},    -- [tostring(item id)] = count
    gil     = 0,      -- your share of gil dropped by mobs
    gilDrops = 0,     -- how many kills paid out that gil
    theme   = 'Farplane9',  -- window theme (phxui); right-click the window to change it
    -- Fame level (1-9, 10 = maxed) per area; NPC sell prices follow it.
    fame    = T{ SANDORIA = 1, BASTOK = 1, WINDURST = 1, NORG = 1 },
};

local MSG_OBTAINS_GIL = 565;

local loot = {
    session     = settings.load(default_session, ALIAS),
    pool        = {},   -- [pool slot] = item id
    last_tick   = nil,
    dirty       = false,
    last_save   = 0,
    reset_armed = 0,
    show_fame   = false,
    show_theme  = false,
};

-- Best price rank you can sell at, and where.
local function sell_rank()
    return fame.bestSellRank(loot.session.fame or {});
end

local function sell_place(area)
    if area == 'JEUNO' then
        return 'Jeuno or any shop without fame pricing';
    end
    return fame.AREA_LABELS[area] .. ' shops';
end

local function save_now()
    settings.save(ALIAS);
    loot.dirty = false;
    loot.last_save = os.time();
end

if ui.adoptPackTheme(loot.session, 'theme') then settings.save(ALIAS); end
ui.setTheme(loot.session.theme);

settings.register(ALIAS, 'npcgil_phx_session_update', function (s)
    if (s ~= nil) then
        loot.session = s;
        if ui.adoptPackTheme(loot.session, 'theme') then settings.save(ALIAS); end
        ui.setTheme(loot.session.theme);
    end
end);

function loot.SetTheme(name)
    loot.session.theme = ui.setTheme(name);
    save_now();
end

local function my_server_id()
    local ok, id = pcall(function ()
        return AshitaCore:GetMemoryManager():GetParty():GetMemberServerId(0);
    end);
    return ok and id or 0;
end

local function item_name(item_id)
    local item = AshitaCore:GetResourceManager():GetItemById(item_id);
    if (item ~= nil) and (item.Name[1] ~= nil) and (item.Name[1] ~= '') then
        return item.Name[1];
    end
    return string.format('Item #%d', item_id);
end

local function format_gil(value)
    local text = tostring(math.floor(value));
    while true do
        local replaced, count = text:gsub('^(%d+)(%d%d%d)', '%1,%2');
        text = replaced;
        if (count == 0) then
            return text;
        end
    end
end

local function format_time(seconds)
    seconds = math.floor(seconds);
    return string.format('%d:%02d:%02d', math.floor(seconds / 3600), math.floor(seconds / 60) % 60, seconds % 60);
end

-- One line per quest that takes this item: "3 to Parvipon (Southern San d'Oria): The Merchants Bidding".
local function quest_lines(quests, have)
    local out = {};
    for _, q in ipairs(quests) do
        local who = q.npc and (q.npc:find('^qm') and 'a ??? spot' or q.npc) or 'an NPC';
        local line = string.format('%s  -  trade %d to %s', q.quest, q.qty, who);
        if q.zone then line = line .. ' (' .. q.zone .. ')'; end
        local extra = {};
        if q.gil then extra[#extra + 1] = format_gil(q.gil) .. ' gil'; end
        if q.repeatable then extra[#extra + 1] = 'repeatable'; end
        if have and have >= q.qty then extra[#extra + 1] = 'you have enough'; end
        if #extra > 0 then line = line .. '  [' .. table.concat(extra, ', ') .. ']'; end
        out[#out + 1] = line;
    end
    return out;
end

-- Rows sorted by total value, plus the session totals.
local function build_report()
    local rows = {};
    local total = 0;
    local count = 0;
    local rank = sell_rank();
    for key, qty in pairs(loot.session.items) do
        local item_id = tonumber(key);
        if (item_id ~= nil) and (qty > 0) then
            local each = fame.sellPrice(prices[item_id], rank);
            local value = (each or 0) * qty;
            rows[#rows + 1] = { id = item_id, name = item_name(item_id), qty = qty, each = each, value = value,
                                quests = questItems[item_id] };
            total = total + value;
            count = count + qty;
        end
    end
    table.sort(rows, function (a, b)
        if (a.value ~= b.value) then
            return a.value > b.value;
        end
        return a.name < b.name;
    end);
    return rows, total, count;
end

local function per_hour(total)
    local elapsed = loot.session.elapsed;
    if (elapsed < 60) then
        return nil;
    end
    return total * 3600 / elapsed;
end

function loot.IsOpen()
    return loot.session.open == true;
end

function loot.Toggle(open)
    if (open == nil) then
        open = not loot.IsOpen();
    end
    loot.session.open = open;
    save_now();
end

function loot.Reset()
    loot.session.items = T{};
    loot.session.gil = 0;
    loot.session.gilDrops = 0;
    loot.session.elapsed = 0;
    loot.pool = {};
    save_now();
    print(chat.header(addon.name):append(chat.message('Started a new loot session.')));
end

function loot.Report()
    local rows, total, count = build_report();
    local rank, area = sell_rank();
    print(chat.header(addon.name):append(chat.message(string.format('Loot session: %d items worth %s gil to an NPC in %s (selling at %s, price rank %d).',
        count, format_gil(total), format_time(loot.session.elapsed), sell_place(area), rank))));
    for _, row in ipairs(rows) do
        local each = row.each and (format_gil(row.each) .. ' ea') or 'not sellable';
        print(chat.header(addon.name):append(chat.message(string.format('  %s x%d  (%s)  %s', row.name, row.qty, each, format_gil(row.value)))));
        if (row.quests ~= nil) then
            for _, line in ipairs(quest_lines(row.quests, row.qty)) do
                print(chat.header(addon.name):append(chat.message('      quest: ' .. line)));
            end
        end
    end
    local gil = loot.session.gil or 0;
    print(chat.header(addon.name):append(chat.message(string.format('  Gil looted: %s (from %d kills)',
        format_gil(gil), loot.session.gilDrops or 0))));
    print(chat.header(addon.name):append(chat.message(string.format('  Total: %s gil', format_gil(total + gil)))));
    local rate = per_hour(total + gil);
    if (rate ~= nil) then
        print(chat.header(addon.name):append(chat.message(string.format('  ~%s gil/hour', format_gil(rate)))));
    end
end

ashita.events.register('packet_in', 'npcgil_phx_packet', function (e)
    if (e.id == 0x00A) then
        -- Zoning: the server rebuilds the pool with fresh 0x0D2 packets.
        loot.pool = {};
    elseif (e.id == 0x0D2) then
        local item_id = struct.unpack('H', e.data, 0x11);
        local slot    = struct.unpack('B', e.data, 0x15);
        if (item_id ~= 0) and (item_id ~= 0xFFFF) then
            loot.pool[slot] = item_id;
        end
    elseif (e.id == 0x0D3) then
        local winner = struct.unpack('I', e.data, 0x05);
        local slot   = struct.unpack('B', e.data, 0x15);
        local result = struct.unpack('B', e.data, 0x16);
        if (result == 1) and (winner == my_server_id()) and (loot.pool[slot] ~= nil) then
            local key = tostring(loot.pool[slot]);
            loot.session.items[key] = (loot.session.items[key] or 0) + 1;
            loot.dirty = true;
        end
        if (result ~= 0) then
            loot.pool[slot] = nil;
        end
    elseif (e.id == 0x029) then
        local message = bit.band(struct.unpack('H', e.data, 0x19), 0x7FFF);
        if (message == MSG_OBTAINS_GIL) and (struct.unpack('I', e.data, 0x09) == my_server_id()) then
            local amount = struct.unpack('I', e.data, 0x0D);
            if (amount > 0) then
                loot.session.gil = (loot.session.gil or 0) + amount;
                loot.session.gilDrops = (loot.session.gilDrops or 0) + 1;
                loot.dirty = true;
            end
        end
    end
end);

local function player_name()
    local ok, name = pcall(function ()
        return AshitaCore:GetMemoryManager():GetParty():GetMemberName(0);
    end);
    return (ok and name ~= nil and name ~= '') and name or nil;
end

-- A small check mark drawn in the current table cell; brighter when you have enough for a turn-in.
local function draw_check(enough)
    local x, y = imgui.GetCursorScreenPos();
    local h = imgui.GetTextLineHeight();
    local c = enough and ui.color.ok or ui.color.gold;
    local col = imgui.GetColorU32({ c[1], c[2], c[3], enough and 1.0 or 0.8 });
    local dl = imgui.GetWindowDrawList();
    local s = h * 0.62;
    local x0, y0 = x + 4, y + h * 0.52;
    dl:AddLine({ x0, y0 }, { x0 + s * 0.38, y0 + s * 0.36 }, col, 2.0);
    dl:AddLine({ x0 + s * 0.38, y0 + s * 0.36 }, { x0 + s, y0 - s * 0.5 }, col, 2.0);
    imgui.Dummy({ s + 8, h });
end

local function draw_body()
        local C = ui.color;
        local rows, total, count = build_report();
        local gil = loot.session.gil or 0;

        -- Header: who, and how long this session has run.
        imgui.TextColored(C.peach, player_name() or 'Loot session');
        ui.rightText(C.muted, format_time(loot.session.elapsed));

        if (imgui.BeginTable('npcgil_phx_stats', 3, ImGuiTableFlags_SizingStretchSame)) then
            ui.stat('NPC value', format_gil(total), C.gold);
            if (imgui.IsItemHovered()) then
                local rank, area = sell_rank();
                imgui.SetTooltip(string.format('What the best-paying NPC for your fame gives you for these items:\n%s, price rank %d (%+.2f%% vs. base price).\nSet your fame with the Fame button below.',
                    sell_place(area), rank, fame.sellPercent(rank)));
            end
            ui.stat('Gil looted', format_gil(gil), C.gold);
            if (imgui.IsItemHovered()) then
                imgui.SetTooltip(string.format('Your share of gil dropped by mobs (mostly beastmen), from %d kills.', loot.session.gilDrops or 0));
            end
            local rate = per_hour(total + gil);
            ui.stat('Total', format_gil(total + gil), C.ember);
            if (rate ~= nil) then
                imgui.SameLine();
                imgui.TextColored(C.muted, string.format('~%s/hr', format_gil(rate)));
            end
            imgui.EndTable();
        end

        ui.section(string.format('Items (%d)', count));
        if (#rows == 0) then
            imgui.TextColored(C.faint, 'Nothing won yet. Items you win from the treasure pool show up here.');
        elseif (imgui.BeginTable('npcgil_phx_items', 5, bit.bor(ImGuiTableFlags_RowBg, ImGuiTableFlags_BordersInnerH, ImGuiTableFlags_SizingFixedFit))) then
            imgui.TableSetupColumn('Item');
            imgui.TableSetupColumn('Qty');
            imgui.TableSetupColumn('Each');
            imgui.TableSetupColumn('Total');
            imgui.TableSetupColumn('Quest');
            imgui.TableHeadersRow();
            for _, row in ipairs(rows) do
                imgui.TableNextRow();
                imgui.TableNextColumn();
                imgui.TextColored(C.secondary, row.name);
                imgui.TableNextColumn();
                imgui.Text(tostring(row.qty));
                imgui.TableNextColumn();
                if (row.each ~= nil) then
                    imgui.TextColored(C.muted, format_gil(row.each));
                else
                    imgui.TextColored(C.faint, '--');
                    ui.tooltip('NPCs won\'t buy this item.');
                end
                imgui.TableNextColumn();
                imgui.TextColored(row.each and C.gold or C.faint, format_gil(row.value));
                imgui.TableNextColumn();
                if (row.quests ~= nil) then
                    local enough = false;
                    for _, q in ipairs(row.quests) do
                        if row.qty >= q.qty then enough = true; end
                    end
                    draw_check(enough);
                    if (imgui.IsItemHovered()) then
                        imgui.SetTooltip('Quest turn-in:\n' .. table.concat(quest_lines(row.quests, row.qty), '\n'));
                    end
                end
            end
            imgui.EndTable();
        end

        imgui.Spacing();
        -- Two clicks within 3 seconds, so a stray click can't wipe a session.
        local armed = (os.time() - loot.reset_armed) < 3;
        if (imgui.Button(armed and 'Click again to reset##npcgil_phx_reset' or 'New session##npcgil_phx_reset')) then
            if armed then
                loot.reset_armed = 0;
                loot.Reset();
            else
                loot.reset_armed = os.time();
            end
        end
        imgui.SameLine();
        if (ui.button('Report to chat##npcgil_phx_report')) then
            loot.Report();
        end
        imgui.SameLine();
        if (ui.toggle('Fame##npcgil_phx_fame', loot.show_fame)) then
            loot.show_fame = not loot.show_fame;
        end
        imgui.SameLine();
        if (ui.toggle('Theme##npcgil_phx_theme', loot.show_theme)) then
            loot.show_theme = not loot.show_theme;
        end

        if loot.show_theme then
            ui.section('Theme');
            imgui.PushItemWidth(160);
            if ui.themeCombo('##npcgil_phx_theme_combo', loot.session.theme) then
                loot.SetTheme(ui.theme);
            end
            imgui.PopItemWidth();
            imgui.SameLine();
            imgui.TextColored(C.muted, '(also: right-click the window, or /npcgil theme <name>)');
        end

        if loot.show_fame then
            ui.section('Fame');
            imgui.TextColored(C.muted, 'Your fame level in each area (ask a fame NPC).');
            loot.session.fame = loot.session.fame or T{};
            for _, a in ipairs(fame.AREAS) do
                local level = loot.session.fame[a.key] or 1;
                imgui.PushItemWidth(100);
                if (imgui.BeginCombo(a.label .. '##npcgil_phx_fame_' .. a.key, fame.levelLabel(level))) then
                    for lv = 1, 10 do
                        if (imgui.Selectable(fame.levelLabel(lv), lv == level)) then
                            loot.session.fame[a.key] = lv;
                            loot.dirty = true;
                        end
                    end
                    imgui.EndCombo();
                end
                imgui.PopItemWidth();
            end
            local rank, area = sell_rank();
            ui.labelValue('Best place to sell:', string.format('%s (rank %d, %+.2f%%)', sell_place(area), rank, fame.sellPercent(rank)), C.peach);
            ui.tooltip('Jeuno shops always pay the full base price (rank 11). A nation or Norg shop pays up to\n2.5% more once your fame there is high; fame with one nation lowers your rank with the other two.');
        end
end

local function draw_window()
    local is_open = { true };
    local token = ui.push();
    if (imgui.Begin('npcgil-phx##npcgil_phx', is_open, bit.bor(ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoCollapse))) then
        -- Guard inside Begin/End so an error can't leave the ImGui window stack unbalanced.
        local ok, err = pcall(draw_body);
        if not ok then
            imgui.TextColored(ui.color.bad, 'npcgil-phx error: ' .. tostring(err));
        end
        if ui.themeMenu(loot.session.theme) then
            loot.SetTheme(ui.theme);
        end
    end
    imgui.End();
    ui.pop(token);

    if not is_open[1] then
        loot.Toggle(false);
    end
end

-- Called every frame by npcgil_phx.lua, whether or not the window is open, so the
-- session timer and saves keep running while you farm.
function loot.Tick()
    -- Session time only runs while a character is logged in.
    local now = os.time();
    if (my_server_id() ~= 0) and (loot.last_tick ~= nil) then
        loot.session.elapsed = loot.session.elapsed + (now - loot.last_tick);
    end
    loot.last_tick = now;

    -- Persist drops within a few seconds, and the timer about once a minute.
    if (loot.dirty and (now - loot.last_save) > 5) or ((now - loot.last_save) > 60) then
        save_now();
    end

    if loot.IsOpen() then
        draw_window();
    end
end

function loot.Save()
    save_now();
end

return loot;
