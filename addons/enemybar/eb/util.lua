--[[
    enemybar - small shared helpers: colors and entity lookups.
]]

require('common');
local imgui = require('imgui');

local M = {};

M.SPAWN_FLAG_PLAYER = 0x0001;
M.SPAWN_FLAG_NPC    = 0x0002;
M.MAX_ENTITIES      = 0x900;

-- {r, g, b, a} floats -> ARGB integer (imtext colors).
function M.toARGB(c, alphaScale)
    local a = math.floor(math.max(0, math.min(1, (c[4] or 1) * (alphaScale or 1))) * 255 + 0.5);
    local r = math.floor(math.max(0, math.min(1, c[1] or 1)) * 255 + 0.5);
    local g = math.floor(math.max(0, math.min(1, c[2] or 1)) * 255 + 0.5);
    local b = math.floor(math.max(0, math.min(1, c[3] or 1)) * 255 + 0.5);
    return bit.bor(bit.lshift(a, 24), bit.lshift(r, 16), bit.lshift(g, 8), b);
end

-- {r, g, b, a} floats -> ImGui packed color (draw-list colors).
function M.toU32(c, rgbScale)
    local s = rgbScale or 1;
    return imgui.GetColorU32({ (c[1] or 1) * s, (c[2] or 1) * s, (c[3] or 1) * s, c[4] or 1 });
end

function M.rgb255(r, g, b, a)
    return { r / 255, g / 255, b / 255, (a or 255) / 255 };
end

function M.copyColor(c)
    return { c[1], c[2], c[3], c[4] };
end

function M.entityManager()
    return AshitaCore:GetMemoryManager():GetEntity();
end

function M.party()
    return AshitaCore:GetMemoryManager():GetParty();
end

function M.myServerId()
    local ok, id = pcall(function() return M.party():GetMemberServerId(0); end);
    return ok and id or 0;
end

function M.myIndex()
    local ok, idx = pcall(function() return M.party():GetMemberTargetIndex(0); end);
    return ok and idx or 0;
end

-- Entity at a target index, or nil when the slot is empty / not rendered.
function M.entity(index)
    if index == nil or index <= 0 or index >= M.MAX_ENTITIES then return nil; end
    local e = GetEntity(index);
    if e == nil or e.Name == nil or e.Name == '' then return nil; end
    return e;
end

-- Target index for a server id, scanning the entity table (cached by libs.packets).
local packets = require('libs.packets');
function M.indexFromId(serverId)
    if serverId == nil or serverId == 0 then return nil; end
    local index = packets.GetIndexFromId(serverId);
    if index == nil or index == 0 then return nil; end
    return index;
end

-- Server id -> NPC/mob? Mirrors enemybar2: players are below 0x01000000, and pets use
-- the 0x700+ index range; anything else is an NPC unless its entity says player.
function M.isNpcId(serverId)
    if serverId == nil or serverId == 0 then return false; end
    if serverId < 0x01000000 then return false; end
    if bit.band(serverId, 0xFFF) >= 0x700 then return false; end
    local index = M.indexFromId(serverId);
    local e = index and M.entity(index);
    if e == nil then return false; end
    return bit.band(e.SpawnFlags, M.SPAWN_FLAG_PLAYER) == 0;
end

function M.isPlayerEntity(e)
    return e ~= nil and bit.band(e.SpawnFlags, M.SPAWN_FLAG_PLAYER) ~= 0;
end

function M.isNpcEntity(e)
    return e ~= nil and bit.band(e.SpawnFlags, M.SPAWN_FLAG_NPC) ~= 0;
end

function M.distance(e)
    if e == nil or e.Distance == nil then return nil; end
    return math.sqrt(e.Distance);
end

return M;
