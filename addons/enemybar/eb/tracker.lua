--[[
    enemybar - party/claim state, aggro ("attention") tracking, focus target and
    target-of-target resolution.

    Ported from enemybar2 (Copyright (c) 2015, Mike McKee; BSD 3-Clause, see NOTICE.md),
    actionTracking.lua and enemybar2.lua, with these fixes:
      - only 0x029 is read as a battle message (enemybar2 parsed every packet as one);
      - pets cached from party members carry their party number (enemybar2 could crash);
      - the cleanup tick runs on a 1 s clock instead of every 75 frames.
    Debuffs, casts and the mob's last action target now come from XIUI's handlers.
]]

require('common');
local util = require('eb.util');
local actionTracker = require('handlers.actiontracker');

local M = {
    members = {},      -- [serverId] = { party = 1|2|3, pet = bool }
    enmity = {},       -- [mob serverId] = { pc = serverId|nil, time = os.time() }
    focusId = nil,     -- server id of the focus target
    nextPartyRefresh = 0,
    nextCleanup = 0,
};

local ATTENTION_FORGET = 3;     -- seconds before an entry is re-checked
local ATTENTION_RANGE = 50;     -- yalms from you before a mob drops off the stack
local CC_STATUS = { [2] = true, [19] = true, [193] = true, [7] = true, [28] = true };

-- ---------------------------------------------------------------------------
-- Party / alliance cache (18 slots: 0-5 party, 6-11 alliance 2, 12-17 alliance 3)
-- ---------------------------------------------------------------------------
function M.RefreshParty()
    local members = {};
    local party = util.party();
    for slot = 0, 17 do
        local ok, active = pcall(function() return party:GetMemberIsActive(slot); end);
        if ok and active == 1 then
            local id = party:GetMemberServerId(slot);
            if id ~= 0 then
                local partyNo = math.floor(slot / 6) + 1;
                members[id] = { party = partyNo };
                local index = party:GetMemberTargetIndex(slot);
                local e = util.entity(index);
                if e ~= nil and e.PetTargetIndex ~= nil and e.PetTargetIndex ~= 0 then
                    local pet = util.entity(e.PetTargetIndex);
                    if pet ~= nil then members[pet.ServerId] = { party = partyNo, pet = true }; end
                end
            end
        end
    end
    M.members = members;
end

function M.IsPartyOrPet(serverId)
    return serverId ~= nil and M.members[serverId] ~= nil;
end

-- 1 = me, 2 = my party (or its pets), 3 = alliance, 0 = anyone else. Claim ids carry
-- only the low 16 bits of the claimer's server id.
local function claimOwner(claimId)
    if claimId == 0 then return 0; end
    local me = util.myServerId();
    if bit.band(me, 0xFFFF) == claimId then return 1; end
    for id, member in pairs(M.members) do
        if bit.band(id, 0xFFFF) == claimId then
            return member.party == 1 and 2 or 3;
        end
    end
    return 0;
end

-- Category used for name colors and claim-based bar colors:
-- dead, party (claimed by you or your party), alliance, member (party member or pet),
-- player, npc, unclaimed, others (claimed by someone outside your alliance).
function M.ClaimState(e, index)
    if e == nil then return 'unclaimed'; end
    if e.HPPercent == 0 then return 'dead'; end
    local claimId = 0;
    pcall(function() claimId = bit.band(util.entityManager():GetClaimStatus(index), 0xFFFF); end);
    local owner = claimOwner(claimId);
    if owner == 1 or owner == 2 then return 'party'; end
    if owner == 3 then return 'alliance'; end
    if M.IsPartyOrPet(e.ServerId) and e.ServerId ~= util.myServerId() then return 'member'; end
    if util.isPlayerEntity(e) then return 'player'; end
    if util.isNpcEntity(e) then return 'npc'; end
    if claimId == 0 then return 'unclaimed'; end
    return 'others';
end

-- ---------------------------------------------------------------------------
-- Attention / aggro tracking (enemybar2's track_enmity)
-- ---------------------------------------------------------------------------
function M.HandleActionPacket(ap)
    if ap == nil or ap.Targets == nil then return; end
    local actor = ap.UserId;
    for _, target in ipairs(ap.Targets) do
        local pc, mob = nil, nil;
        if M.IsPartyOrPet(actor) then pc = actor; elseif util.isNpcId(actor) then mob = actor; end
        if M.IsPartyOrPet(target.Id) then pc = target.Id; elseif util.isNpcId(target.Id) then mob = target.Id; end
        if pc ~= nil and mob ~= nil then
            -- A mob acting on your party sets who it's attacking; your party acting on a mob
            -- only adds it to the stack if it isn't tracked yet.
            if actor == mob or M.enmity[mob] == nil then
                M.enmity[mob] = { pc = pc, time = os.time() };
                if actor == mob then return; end
            end
        end
    end
end

-- Is entity a facing entity b? enemybar2's heading test (accepts facing directly away too).
local function lookingAt(aIndex, bIndex)
    local ok, result = pcall(function()
        local em = util.entityManager();
        local ax, ay = em:GetLocalPositionX(aIndex), em:GetLocalPositionY(aIndex);
        local bx, by = em:GetLocalPositionX(bIndex), em:GetLocalPositionY(bIndex);
        local h = em:GetLocalPositionYaw(aIndex) % math.pi;
        local h2 = (math.atan2(ax - bx, ay - by) + math.pi / 2) % math.pi;
        return math.abs(h - h2) < 0.15;
    end);
    if not ok then return true; end  -- no heading available: keep the pairing
    return result;
end

local function cleanup(now)
    for mobId, entry in pairs(M.enmity) do
        if now - entry.time >= ATTENTION_FORGET then
            local index = util.indexFromId(mobId);
            local mob = index and util.entity(index);
            if mob == nil or mob.HPPercent == 0 or mob.Status == 0 then
                M.enmity[mobId] = nil;
            else
                local dist = util.distance(mob);
                if dist ~= nil and dist > ATTENTION_RANGE then
                    M.enmity[mobId] = nil;
                elseif entry.pc ~= nil then
                    local pcIndex = util.indexFromId(entry.pc);
                    if pcIndex == nil or not lookingAt(index, pcIndex) then entry.pc = nil; end
                end
            end
        end
    end
end

function M.Tick()
    local now = os.time();
    if now >= M.nextPartyRefresh then
        M.RefreshParty();
        M.nextPartyRefresh = now + 1;
    end
    if now >= M.nextCleanup then
        cleanup(now);
        M.nextCleanup = now + 1;
    end
end

function M.HandleZone()
    M.enmity = {};
    M.nextPartyRefresh = 0;
end

-- Mobs on the aggro stack, lowest HP first; crowd-controlled mobs go last (enemybar2's order).
-- debuffsOf(serverId) returns XIUI's tracked status ids for the CC check.
function M.OrderedAggro(debuffsOf)
    local normal, controlled = {}, {};
    for mobId, entry in pairs(M.enmity) do
        local index = util.indexFromId(mobId);
        local mob = index and util.entity(index);
        if mob ~= nil and mob.HPPercent > 0 then
            local row = { id = mobId, index = index, entity = mob, pc = entry.pc, hpp = mob.HPPercent };
            local isCC = false;
            local ids = debuffsOf and debuffsOf(mobId);
            for _, statusId in ipairs(ids or {}) do
                if CC_STATUS[statusId] then isCC = true; break; end
            end
            table.insert(isCC and controlled or normal, row);
        end
    end
    local byHp = function(a, b) return a.hpp < b.hpp; end;
    table.sort(normal, byHp);
    table.sort(controlled, byHp);
    for _, row in ipairs(controlled) do normal[#normal + 1] = row; end
    return normal;
end

-- ---------------------------------------------------------------------------
-- Target of target
-- ---------------------------------------------------------------------------
-- Who the entity at `index` is targeting: for players, their own target; for mobs, the
-- last thing they acted on (XIUI's action tracker), then who they're facing on the
-- aggro stack, then the game's targeted index.
function M.TargetOf(index)
    local e = util.entity(index);
    if e == nil then return nil; end
    local totIndex;
    if util.isPlayerEntity(e) then
        totIndex = e.TargetedIndex;
    else
        totIndex = actionTracker.GetLastTarget(e.ServerId);
        if totIndex == nil and M.enmity[e.ServerId] and M.enmity[e.ServerId].pc then
            totIndex = util.indexFromId(M.enmity[e.ServerId].pc);
        end
        if totIndex == nil then totIndex = e.TargetedIndex; end
    end
    if totIndex == nil or totIndex == 0 then return nil; end
    return totIndex;
end

-- ---------------------------------------------------------------------------
-- Focus target
-- ---------------------------------------------------------------------------
function M.FocusIndex()
    if M.focusId == nil then return nil; end
    return util.indexFromId(M.focusId);
end

-- Find an entity by name: exact match, then prefix, then substring (case-insensitive).
function M.FindByName(name)
    local needle = name:lower();
    local prefix, contains = nil, nil;
    for index = 1, util.MAX_ENTITIES - 1 do
        local e = util.entity(index);
        if e ~= nil then
            local n = e.Name:lower();
            if n == needle then return index, e; end
            if prefix == nil and n:sub(1, #needle) == needle then prefix = index; end
            if contains == nil and n:find(needle, 1, true) then contains = index; end
        end
    end
    local hit = prefix or contains;
    return hit, hit and util.entity(hit);
end

return M;
