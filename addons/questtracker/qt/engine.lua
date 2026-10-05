--[[
    questtracker - which quests you have, and where you are in each.

    The server tells the client about your quests in packet 0x056 (LandSandBoat
    src/map/packets/s2c/0x056_mission_other.cpp): 32 bytes of flags, one bit per quest id, then a
    port saying which log they are. Ports 0x50, 0x58 ... 0x80 are the current quests of San d'Oria,
    Bastok, Windurst, Jeuno, Other Areas, Outlands and Aht Urhgan (log 0-6); 0x90 ... 0xC0 the
    completed ones. It is sent at login, on zoning and whenever a quest is added or completed.

    The server never sends a quest's progress, so steps are inferred and saved per character:
      - events: packets 0x032 / 0x034 start a cutscene with the zone (EventNum) and cutscene id
        (EventPara). A step finishes when one of its events plays;
      - key items: holding a step's key item means that step is done (checked every 2 s);
      - you can mark a step done or undo it by hand.
    A step can only move forward on its own, and matching a later step also skips the ones before.
]]

require('common');

local quests = require('data.quests');
-- Hand-written steps: data/overrides_jobs.lua (job unlock quests), then data/overrides.lua (wins).
local overrides = {};
for _, name in ipairs({ 'data.overrides_jobs', 'data.overrides' }) do
    local ok, t = pcall(require, name);
    if ok and type(t) == 'table' then
        for k, v in pairs(t) do overrides[k] = v; end
    end
end

local M = {
    quests = quests,
    LOG_LABELS = { [0] = "San d'Oria", 'Bastok', 'Windurst', 'Jeuno', 'Other Areas', 'Outlands', 'Aht Urhgan' },
    cfg = nil,           -- set by questtracker.lua (the saved settings)
    onNotify = nil,      -- function(text) for chat notices
};

-- Hand-written wording (data/overrides.lua) replaces generated step text by index.
do
    local byKey = {};
    for id, q in pairs(quests) do byKey[q.key] = id; end
    for key, ov in pairs(overrides) do
        local q = quests[byKey[key] or -1];
        if q then
            if ov.note then q.note = ov.note; end
            if type(ov.replace) == 'table' then
                -- a full hand-written step list (quests the generator can't read, or reads badly)
                q.steps = ov.replace;
                for _, s in ipairs(q.steps) do s.polished = true; end
            elseif ov.steps and q.steps then
                for i, text in pairs(ov.steps) do
                    if q.steps[i] then q.steps[i].text = text; q.steps[i].polished = true; end
                end
            end
        end
    end
end

local function key(id) return tostring(id); end

function M.quest(id) return quests[tonumber(id)]; end

function M.step(id)
    return M.cfg.step[key(id)] or 1;
end

function M.setStep(id, n, quiet)
    local q = M.quest(id);
    if q == nil or q.steps == nil then return; end
    n = math.max(1, math.min(#q.steps + 1, n));
    local old = M.step(id);
    M.cfg.step[key(id)] = n;
    M.cfg.review[key(id)] = nil;     -- the player has set their progress
    M.cfg.dirty = true;
    if not quiet and n > old and M.onNotify then
        if n > #q.steps then
            M.onNotify(string.format('%s: all steps done. Finish it to complete the quest.', q.name));
        else
            M.onNotify(string.format('%s: next, %s', q.name, q.steps[n].text));
        end
    end
end

function M.isActive(id) return M.cfg.active[key(id)] == true; end
function M.isTracked(id) return M.cfg.tracked[key(id)] == true; end

function M.setTracked(id, on)
    M.cfg.tracked[key(id)] = on and true or nil;
    M.cfg.dirty = true;
end

-- Quests found already in progress (in your log the first time, or added by hand): the addon
-- doesn't know how far along you are until you tick the steps you've done.
function M.needsReview(id) return M.cfg.review[key(id)] == true; end
function M.reviewed(id) M.cfg.review[key(id)] = nil; M.cfg.dirty = true; end

-- Add a quest you're on before the server's quest log has arrived. The next quest log the server
-- sends is authoritative and drops it again if you aren't actually on it.
function M.addManual(id)
    if quests[id] == nil then return; end
    M.cfg.active[key(id)] = true;
    M.cfg.tracked[key(id)] = true;
    M.cfg.review[key(id)] = true;
    M.cfg.dirty = true;
end

-- Quests you aren't on whose name contains text (case-insensitive), best matches first.
function M.search(text, limit)
    local needle = (text or ''):lower();
    if #needle < 2 then return {}; end
    local starts, contains = {}, {};
    for id, q in pairs(quests) do
        if not M.cfg.active[key(id)] and not M.cfg.completed[key(id)] then
            local n = q.name:lower();
            local at = n:find(needle, 1, true);
            if at == 1 then starts[#starts + 1] = id; elseif at then contains[#contains + 1] = id; end
        end
    end
    local byName = function (a, b) return quests[a].name < quests[b].name; end;
    table.sort(starts, byName);
    table.sort(contains, byName);
    for _, id in ipairs(contains) do starts[#starts + 1] = id; end
    while #starts > (limit or 8) do table.remove(starts); end
    return starts;
end

-- Active quests (ids), sorted by log then name.
function M.activeList()
    local list = {};
    for k in pairs(M.cfg.active) do
        local id = tonumber(k);
        if id and quests[id] then list[#list + 1] = id; end
    end
    table.sort(list, function (a, b)
        local qa, qb = quests[a], quests[b];
        if qa.log ~= qb.log then return qa.log < qb.log; end
        return qa.name < qb.name;
    end);
    return list;
end

function M.trackedList()
    local list = {};
    for _, id in ipairs(M.activeList()) do
        if M.isTracked(id) then list[#list + 1] = id; end
    end
    return list;
end

-- ---------------------------------------------------------------------------
-- Packets
-- ---------------------------------------------------------------------------
local function flagsOf(data)
    local set = {};
    for byte = 0, 31 do
        local b = data:byte(0x04 + byte + 1) or 0;
        if b ~= 0 then
            for bitn = 0, 7 do
                if bit.band(b, bit.lshift(1, bitn)) ~= 0 then set[byte * 8 + bitn] = true; end
            end
        end
    end
    return set;
end

local function onCurrent(log, set)
    local cfg = M.cfg;
    local first = not cfg.seenLogs[key(log)];
    cfg.seenLogs[key(log)] = true;
    -- forget quests of this log that are no longer current (completion comes on its own port)
    for k in pairs(cfg.active) do
        local id = tonumber(k);
        if id and math.floor(id / 1000) == log and not set[id % 1000] then cfg.active[k] = nil; end
    end
    for qid in pairs(set) do
        local id = log * 1000 + qid;
        if quests[id] and not cfg.active[key(id)] then
            cfg.active[key(id)] = true;
            if first then cfg.review[key(id)] = true; end   -- already in progress: ask for its steps
            if not first then
                if cfg.autoTrack then M.setTracked(id, true); end
                if M.onNotify then M.onNotify('New quest: ' .. quests[id].name .. (cfg.autoTrack and ' (tracking)' or '')); end
            end
        end
    end
    cfg.dirty = true;
end

local function onComplete(log, set)
    local cfg = M.cfg;
    for qid in pairs(set) do
        local id = log * 1000 + qid;
        local k = key(id);
        if not cfg.completed[k] then
            local wasMine = cfg.active[k] or cfg.tracked[k];
            cfg.completed[k] = true;
            cfg.active[k], cfg.tracked[k], cfg.step[k], cfg.review[k] = nil, nil, nil, nil;
            if wasMine and quests[id] and M.onNotify then M.onNotify('Quest complete: ' .. quests[id].name .. '!'); end
        end
    end
    cfg.dirty = true;
end

local function onEvent(zone, csid)
    for k in pairs(M.cfg.active) do
        local id = tonumber(k);
        local q = id and quests[id];
        if q and q.steps then
            local cur = M.step(id);
            for i = cur, #q.steps do
                local hit = false;
                for _, ev in ipairs(q.steps[i].ev or {}) do
                    if ev[1] == zone and ev[2] == csid then hit = true; break; end
                end
                if hit then M.setStep(id, i + 1); break; end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- Kill counters. A step can carry kills = { n = 100, mob = 'Copper Quadav', zone = 147,
-- weapon = 16607, melee = true, party = true }: count defeats ("<actor> defeats <target>",
-- battle message 6 in packet 0x029) that match, and finish the step at n. The server keeps its
-- own hidden count; this one starts when the addon sees the step and can be adjusted by hand.
-- ---------------------------------------------------------------------------
local MSG_DEFEATS = 6;
local lastMelee = {};    -- [target server id] = os.clock() of your last melee hit on it

function M.kills(id) return M.cfg.kills[key(id)] or 0; end

function M.setKills(id, n)
    local q = quests[id];
    local s = q and q.steps and q.steps[M.step(id)];
    if not (s and s.kills) then return; end
    n = math.max(0, n);
    M.cfg.kills[key(id)] = n;
    M.cfg.dirty = true;
    if n >= s.kills.n then
        M.cfg.kills[key(id)] = nil;
        M.setStep(id, M.step(id) + 1);
    end
end

local function myId()
    return AshitaCore:GetMemoryManager():GetParty():GetMemberServerId(0);
end

local function inParty(serverId)
    local party = AshitaCore:GetMemoryManager():GetParty();
    for slot = 0, 5 do
        if party:GetMemberIsActive(slot) == 1 and party:GetMemberServerId(slot) == serverId then return true; end
    end
    return false;
end

local function mainWeapon()
    local inv = AshitaCore:GetMemoryManager():GetInventory();
    local eq = inv:GetEquippedItem(0);
    if eq == nil then return 0; end
    local index = bit.band(eq.Index, 0x00FF);
    if index == 0 then return 0; end
    local item = inv:GetContainerItem(bit.rshift(bit.band(eq.Index, 0xFF00), 8), index);
    return item and item.Id or 0;
end

-- Your melee hits (action category 1), so a melee-only counter knows how the target died.
local function onAction(e)
    local data, offset, maxBits = e.data_raw, 40, e.size * 8;
    local function bits(n)
        if offset + n > maxBits then return nil; end
        local v = ashita.bits.unpack_be(data, 0, offset, n);
        offset = offset + n;
        return v;
    end
    local actor = bits(32);
    local targets = bits(6);
    offset = offset + 4;
    local category = bits(4);
    if category ~= 1 or actor ~= myId() or (targets or 0) == 0 then return; end
    bits(32); bits(32);                 -- param, recast
    local target = bits(32);
    if target then lastMelee[target] = os.clock(); end
end

local function onDefeat(actor, target, targetIndex)
    local name, zone, weapon = nil, M.currentZone(), nil;
    for k in pairs(M.cfg.active) do
        local id = tonumber(k);
        local q = id and quests[id];
        local s = q and q.steps and q.steps[M.step(id)];
        local kl = s and s.kills;
        if kl then
            local ok = kl.party and inParty(actor) or actor == myId();
            if ok and kl.zone then ok = zone == kl.zone; end
            if ok and kl.mob then
                name = name or AshitaCore:GetMemoryManager():GetEntity():GetName(targetIndex) or '';
                ok = name:lower() == kl.mob:lower();
            end
            if ok and kl.weapon then
                weapon = weapon or mainWeapon();
                ok = weapon == kl.weapon;
            end
            if ok and kl.melee then ok = lastMelee[target] ~= nil and os.clock() - lastMelee[target] < 3; end
            if ok then M.setKills(id, M.kills(id) + 1); end
        end
    end
    lastMelee[target] = nil;
end

function M.HandlePacket(e)
    if e.id == 0x028 then
        onAction(e);
    elseif e.id == 0x029 then
        local message = bit.band(struct.unpack('H', e.data, 0x18 + 1), 0x7FFF);
        if message == MSG_DEFEATS then
            onDefeat(struct.unpack('I', e.data, 0x04 + 1), struct.unpack('I', e.data, 0x08 + 1),
                struct.unpack('H', e.data, 0x16 + 1));
        end
    elseif e.id == 0x056 then
        local port = struct.unpack('H', e.data, 0x24 + 1);
        if port >= 0x50 and port <= 0x80 and port % 8 == 0 then
            onCurrent((port - 0x50) / 8, flagsOf(e.data));
        elseif port >= 0x90 and port <= 0xC0 and port % 8 == 0 then
            onComplete((port - 0x90) / 8, flagsOf(e.data));
        end
    elseif e.id == 0x032 then
        onEvent(struct.unpack('H', e.data, 0x0A + 1), struct.unpack('H', e.data, 0x0C + 1));
    elseif e.id == 0x034 then
        onEvent(struct.unpack('H', e.data, 0x2A + 1), struct.unpack('H', e.data, 0x2C + 1));
    end
end

-- Holding a later step's key item means everything before it is done.
function M.CheckKeyItems()
    local player = AshitaCore:GetMemoryManager():GetPlayer();
    for k in pairs(M.cfg.active) do
        local id = tonumber(k);
        local q = id and quests[id];
        if q and q.steps then
            local cur = M.step(id);
            for i = #q.steps, cur, -1 do
                local ki = q.steps[i].ki;
                if ki and player:HasKeyItem(ki) then
                    M.setStep(id, i + 1);
                    break;
                end
            end
        end
    end
end

-- ---------------------------------------------------------------------------
-- What the current step needs: items to trade (step.need = { { id, qty, name } }) and key items
-- (step.needKi = { { id, name } }), checked against your bags and key items once a second.
-- ---------------------------------------------------------------------------
local counts, held, nextScan = {}, {}, 0;
local CONTAINERS = 16;   -- inventory (0), then safe, storage, locker, satchel, sack, case, wardrobes

local function scan()
    local want, wantKi = {}, {};
    for _, id in ipairs(M.trackedList()) do
        local q = quests[id];
        local s = q.steps and q.steps[M.step(id)];
        if s then
            for _, n in ipairs(s.need or {}) do want[n[1]] = true; end
            for _, k in ipairs(s.needKi or {}) do wantKi[k[1]] = true; end
        end
    end
    counts, held = {}, {};
    if next(want) then
        local inv = AshitaCore:GetMemoryManager():GetInventory();
        for c = 0, CONTAINERS do
            local ok, max = pcall(function () return inv:GetContainerCountMax(c); end);
            if ok and max and max > 0 then
                for i = 0, max do
                    local item = inv:GetContainerItem(c, i);
                    if item and item.Id ~= 0 and want[item.Id] then
                        local e = counts[item.Id] or { inv = 0, other = 0 };
                        if c == 0 then e.inv = e.inv + item.Count; else e.other = e.other + item.Count; end
                        counts[item.Id] = e;
                    end
                end
            end
        end
    end
    if next(wantKi) then
        local player = AshitaCore:GetMemoryManager():GetPlayer();
        for kid in pairs(wantKi) do held[kid] = player:HasKeyItem(kid) and true or false; end
    end
end

-- { { text, ok } } for a quest's current step.
function M.Requirements(id)
    local now = os.clock();
    if now >= nextScan then
        nextScan = now + 1;
        pcall(scan);
    end
    local q = quests[id];
    local s = q and q.steps and q.steps[M.step(id)];
    local out = {};
    if not s then return out; end
    for _, n in ipairs(s.need or {}) do
        local c = counts[n[1]] or { inv = 0, other = 0 };
        local text = string.format('%d/%d %s', math.min(c.inv, n[2]), n[2], n[3]);
        if c.inv < n[2] and c.other > 0 then
            text = text .. string.format(' (%d in another bag)', c.other);
        end
        out[#out + 1] = { text, c.inv >= n[2] };
    end
    for _, k in ipairs(s.needKi or {}) do
        out[#out + 1] = { k[2] .. ' (key item)', held[k[1]] == true };
    end
    if s.kills then
        local kl = s.kills;
        local what = kl.mob and (kl.mob .. ' kills') or 'Kills';
        out[#out + 1] = { string.format('%s: %d/%d', what, math.min(M.kills(id), kl.n), kl.n), M.kills(id) >= kl.n };
    end
    return out;
end

function M.currentZone()
    local ok, z = pcall(function () return AshitaCore:GetMemoryManager():GetParty():GetMemberZone(0); end);
    return ok and z or 0;
end

return M;
