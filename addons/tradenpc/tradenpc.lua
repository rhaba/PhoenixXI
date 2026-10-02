--[[
    TradeNPC for Ashita v4 - trade items and gil to an NPC from a command.

    Ported from TradeNPC for Windower 4 by Ivaar (https://github.com/Ivaar/Windower-addons),
    Copyright (c) 2018, Ivaar, BSD 3-Clause (see LICENSE). Ashita port by Spongeh.

    /tradenpc <qty> <item> [<qty> <item> ...] [npc name]
    /tradenpc <gil amount> gil [<qty> <item> ...] [npc name]
        Fills the trade with up to 8 item stacks (9 slots with gil) and sends it to your target,
        or to the named NPC within 6 yalms. Quantities are taken from your fullest stacks first,
        split across as many slots as needed (partial stacks are fine).
        e.g. /tradenpc 100 "1 byne bill"
             /tradenpc 3 "ear of millioncorn" Melyon
             /tradenpc 10000 gil 1 "Beastmen's Seal"
]]

addon.name    = 'tradenpc';
addon.author  = 'Ivaar (Windower original); Ashita port by Spongeh';
addon.version = '1.20.09.02-ashita.1';
addon.desc    = 'Trade items and gil to an NPC from a command.';
addon.link    = 'https://github.com/Ivaar/Windower-addons';

require('common');
local chat = require('chat');

local LINKSHELL_ITEMS = { [512] = true, [513] = true, [514] = true, [515] = true };
local INVENTORY = 0;
local MAX_SLOTS = 8;   -- item slots in a trade (gil uses its own ninth slot)

local function msg(text) print(chat.header(addon.name):append(chat.message(text))); end
local function err(text) print(chat.header(addon.name):append(chat.error(text))); end

local function mm() return AshitaCore:GetMemoryManager(); end

-- ---------------------------------------------------------------------------
-- Items
-- ---------------------------------------------------------------------------
-- Lowercase name / log name -> item resource, built on first use.
local itemsByName = nil;

local function buildItemIndex()
    itemsByName = {};
    local res = AshitaCore:GetResourceManager();
    for id = 1, 65534 do
        local item = res:GetItemById(id);
        if item ~= nil and item.Name ~= nil then
            for _, field in ipairs({ item.Name[1], item.LogNameSingular and item.LogNameSingular[1] }) do
                if field ~= nil and field ~= '' then
                    local key = field:lower();
                    if itemsByName[key] == nil then itemsByName[key] = item; end
                end
            end
        end
    end
end

local function findItemResource(name)
    if itemsByName == nil then buildItemIndex(); end
    return itemsByName[name:lower()];
end

-- Best unused inventory slot for item_id (not equipped/bazaared): the fullest stack, so trades
-- use as few slots as possible. Returns slot, count available there.
local function findInventorySlot(itemId, exclude)
    local inv = mm():GetInventory();
    local best, bestCount = nil, 0;
    for slot = 1, inv:GetContainerCountMax(INVENTORY) do
        local entry = inv:GetContainerItem(INVENTORY, slot);
        if entry ~= nil and entry.Id == itemId and entry.Flags == 0 and not exclude[slot] and entry.Count > bestCount then
            best, bestCount = slot, entry.Count;
        end
    end
    return best, bestCount;
end

local function gilOnHand()
    local entry = mm():GetInventory():GetContainerItem(INVENTORY, 0);
    return entry and entry.Count or 0;
end

local function parseGil(text)
    if text:match('%a') then return nil; end
    local n = tonumber((text:gsub('%p', '')));
    if n and n > 0 then return n; end
    return nil;
end

-- ---------------------------------------------------------------------------
-- NPCs
-- ---------------------------------------------------------------------------
local function validTarget(index)
    local ent = mm():GetEntity();
    local dist = ent:GetDistance(index);
    if dist == nil or math.sqrt(dist) >= 6 then return false; end
    if bit.band(ent:GetRenderFlags0(index), 0x200) ~= 0x200 then return false; end  -- not targetable
    return bit.band(ent:GetSpawnFlags(index), 0xDF) == 2;                            -- NPC
end

local function npcInfo(index)
    local ent = mm():GetEntity();
    return { index = index, id = ent:GetServerId(index), name = ent:GetName(index) };
end

local function findNpc(name)
    local ent = mm():GetEntity();
    name = name:lower();
    for index = 1, 0x8FF do
        local n = ent:GetName(index);
        if n ~= nil and n:lower() == name and validTarget(index) then return npcInfo(index); end
    end
    return nil;
end

local function currentTarget()
    local index = mm():GetTarget():GetTargetIndex(0);
    if index ~= nil and index ~= 0 and validTarget(index) then return npcInfo(index); end
    return nil;
end

local function myStatus()
    local index = mm():GetParty():GetMemberTargetIndex(0);
    return mm():GetEntity():GetStatus(index);
end

-- ---------------------------------------------------------------------------
-- The trade packet (client 0x036): u32 target id @0x04, u32 counts[10] @0x08,
-- u8 inventory slots[10] @0x30, u16 target index @0x3A, u8 slot count @0x3C.
-- Ashita fills in the 4-byte header.
-- ---------------------------------------------------------------------------
local function putU32(t, offset, v)
    for i = 0, 3 do t[offset + i + 1] = bit.band(bit.rshift(v, i * 8), 0xFF); end
end

local function sendTrade(target, slots, counts)
    local p = {};
    for i = 1, 0x40 do p[i] = 0; end
    putU32(p, 0x04, target.id);
    for i = 1, 9 do
        putU32(p, 0x08 + (i - 1) * 4, counts[i] or 0);
        p[0x30 + i] = slots[i] or 0;
    end
    p[0x3A + 1] = bit.band(target.index, 0xFF);
    p[0x3B + 1] = bit.band(bit.rshift(target.index, 8), 0xFF);
    p[0x3C + 1] = #slots;
    AshitaCore:GetPacketManager():AddOutgoingPacket(0x36, p);
end

-- ---------------------------------------------------------------------------
-- Command
-- ---------------------------------------------------------------------------
local function usage()
    msg('/tradenpc <qty> <item> [<qty> <item> ...] [npc name]');
    msg('/tradenpc <gil> gil [<qty> <item> ...] [npc name]   e.g. /tradenpc 100 "1 byne bill"');
end

local function plainText(s)
    local ok, out = pcall(function () return AshitaCore:GetChatManager():ParseAutoTranslate(s, false); end);
    return (ok and type(out) == 'string' and out ~= '') and out or s;
end

local function handle(args)
    if #args < 2 then usage(); return; end
    if myStatus() ~= 0 then err('You can only trade while standing (not engaged, resting or in an event).'); return; end

    local target;
    if #args % 2 == 1 then
        target = findNpc(plainText(args[#args]));
        args[#args] = nil;
    else
        target = currentTarget();
    end
    if target == nil then err('No target or too far away.'); return; end

    local slots, counts = {}, {};
    local first = 1;
    if args[2]:lower() == 'gil' then
        local amount = parseGil(args[1]);
        if amount == nil or amount > gilOnHand() then err('Invalid gil amount.'); return; end
        slots[1], counts[1] = 0, amount;
        first = 2;
    end

    local exclude = {};
    for pair = first, 9 do
        local qtyArg, nameArg = args[pair * 2 - 1], args[pair * 2];
        if nameArg == nil then break; end
        local units = tonumber(qtyArg);
        local name = plainText(nameArg);
        local item = findItemResource(name);
        if item == nil or LINKSHELL_ITEMS[item.Id] then
            err(string.format('"%s" is not a valid item name (arg %d).', name, pair * 2)); return;
        end
        if units == nil or units < 1 then
            err(string.format('Invalid quantity (arg %d).', pair * 2 - 1)); return;
        end
        local stack = math.max(1, item.StackSize or 1);
        while units > 0 do
            local slot, have = findInventorySlot(item.Id, exclude);
            local count = math.min(units, stack, have);
            if slot == nil then
                err(string.format('%s x%s not found in inventory.', item.Name[1], qtyArg)); return;
            end
            exclude[slot] = true;
            slots[#slots + 1], counts[#counts + 1] = slot, count;
            units = units - count;
        end
    end

    local num = #slots;
    if num == 0 then usage(); return; end
    if num >= first + MAX_SLOTS then err('Too many items: a trade holds 8 stacks (plus gil).'); return; end
    sendTrade(target, slots, counts);
    msg(string.format('Trading %d slot%s to %s.', num, num == 1 and '' or 's', target.name));
end

ashita.events.register('command', 'tradenpc_command', function (e)
    local args = e.command:args();
    if #args == 0 or args[1]:lower() ~= '/tradenpc' then return; end
    e.blocked = true;
    table.remove(args, 1);
    local ok, why = pcall(handle, args);
    if not ok then err('Error: ' .. tostring(why)); end
end);
