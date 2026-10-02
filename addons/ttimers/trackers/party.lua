--[[
Copyright (c) 2024 Thorny

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
--]]

--[[
    Party job ability recasts (added by Spongeh).

    The server never sends other players' recast timers, but every job ability use arrives as
    an action packet, and LandSandBoat fills that packet's recast field with the ability's real
    recast after the user's merits and recast gear (CCharEntity::OnAbility). Each use starts a
    timer for that party member and recast group:

      - recast from the packet when it is set;
      - otherwise (Blood Pacts, whose recast starts when the pact goes off) the base recast from
        data/partyrecasts.lua, generated from the server's abilities table;
      - charge abilities (Ready, Quick Draw, Stratagems) send the time one use costs, so uses
        stack on the remaining time, the way the server's own recast does.

    Only job abilities: action categories 6 (job ability), 14 (dances / flourishes) and
    15 (rune fencer). Spells and weapon skills are ignored.
]]

local actionPacket = require('actionpacket');
local encoding = require('gdifonts.encoding');
local baseRecasts = require('data.partyrecasts');

local ABILITY_CATEGORIES = { [6] = true, [14] = true, [15] = true };
local CHARGE_RECASTS = { [102] = true, [195] = true, [231] = true };  -- Ready, Quick Draw, Stratagems

local tracker = {};
local state = {
    Timers = T{},   -- [memberId .. ':' .. recastId] = timer
};

-- Party (and optionally alliance) members by server id -> { Name, Job }.
local function partyMembers()
    local members = {};
    local party = AshitaCore:GetMemoryManager():GetParty();
    local last = gSettings.Party.IncludeAlliance and 17 or 5;
    for slot = 0, last do
        if party:GetMemberIsActive(slot) == 1 then
            local id = party:GetMemberServerId(slot);
            if id ~= 0 and (slot ~= 0 or gSettings.Party.IncludeSelf) then
                members[id] = { Name = party:GetMemberName(slot), Job = party:GetMemberMainJob(slot) };
            end
        end
    end
    return members;
end

local function abilityName(id)
    local res = AshitaCore:GetResourceManager():GetAbilityById(id + 0x200);
    if res and res.Name[1] and res.Name[1] ~= '' then
        return encoding:ShiftJIS_To_UTF8(res.Name[1], true);
    end
    return string.format('Ability %u', id);
end

local function abilityIcon(recastId, job)
    local paths = T{
        string.format('abilities/%u_%u.png', recastId, job or 0),
        string.format('abilities/%u.png', recastId),
        'abilities/default.png',
    };
    for _,path in ipairs(paths) do
        if GetFilePath(path) then
            return path;
        end
    end
end

local function startTimer(member, memberId, abilityId, packetRecast)
    local base = baseRecasts[abilityId];
    local recastId = base and base[2] or (0x10000 + abilityId);
    local duration = packetRecast;
    local fromPacket = (duration ~= nil and duration > 0);
    if not fromPacket then
        duration = base and base[1] or 0;
    end
    if duration <= 0 then
        return;
    end

    local now = os.clock();
    local key = string.format('%u:%u', memberId, recastId);
    local existing = state.Timers[key];
    local expiration = now + duration;
    if CHARGE_RECASTS[recastId] and existing and existing.Expiration > now then
        -- Charges: this use's cost stacks on the time still remaining.
        expiration = existing.Expiration + duration;
    end

    local name = abilityName(abilityId);
    local label = gSettings.Party.ShowMemberName and string.format('%s: %s', member.Name, name) or name;
    state.Timers[key] = {
        Creation = now,
        Expiration = expiration,
        TotalDuration = expiration - now,
        Duration = expiration - now,
        Label = label,
        Ability = name,
        Icon = abilityIcon(recastId, member.Job),
        Local = T{},
        Tooltip = string.format('%s used %s.\n%s', member.Name, name,
            fromPacket and 'Recast reported by the server (includes their merits and gear).'
                or 'Base recast from the server data; their merits may shorten it.'),
    };
end

function tracker:Tick()
    local now = os.clock();
    local active = T{};
    for key, timer in pairs(state.Timers) do
        if timer.Local.Delete then
            if timer.Local.Block then
                gSettings.Party.Blocked['Label:' .. timer.Ability] = true;
                settings.save();
                print(chat.header('tTimers') .. chat.message('Blocked party ability: ' .. timer.Ability));
            end
            state.Timers[key] = nil;
        else
            timer.Duration = timer.Expiration - now;
            if gSettings.Party.Blocked['Label:' .. timer.Ability] ~= true then
                active:append(timer);
            end
        end
    end
    return active;
end

ashita.events.register('packet_in', 'party_recast_tracker_handleincomingpacket', function (e)
    if (e.id ~= 0x028) or (not gSettings.Party.Enabled) then
        return;
    end
    local packet = actionPacket:parse(e);
    if (packet == nil) or (not ABILITY_CATEGORIES[packet.Type]) then
        return;
    end
    local member = partyMembers()[packet.UserId];
    if member == nil then
        return;
    end
    local abilityId = packet.Id;
    if abilityId >= 0x200 and abilityId < 0x400 then
        abilityId = abilityId - 0x200;
    end
    startTimer(member, packet.UserId, abilityId, packet.Recast);
end);

return tracker;
