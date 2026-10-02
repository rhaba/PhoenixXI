--[[
    enemybar - mob weaknesses, resistances and status immunities, read at runtime from an
    installed MobDB (addons/mobdb/data/<zone>.lua) and drawn with MobDB's own icons. Nothing
    from MobDB is bundled here; without MobDB the resist row simply doesn't appear.

    MobDB's Modifiers are damage multipliers: 1 is normal, 1.25 takes 25% more, 0.5 takes half.
]]

require('common');

local M = {
    zone = nil,
    data = nil,
};

local MOBDB = string.format('%saddons\\mobdb\\', AshitaCore:GetInstallPath());

local MOD_ORDER = { 'Slashing', 'Piercing', 'H2H', 'Impact', 'Fire', 'Ice', 'Wind', 'Earth', 'Lightning', 'Water', 'Light', 'Dark' };

-- MobDB's immunity bits and their icons (details.lua PrintImmunities).
local IMMUNITIES = {
    { 0x01, 'ImmuneSleep' }, { 0x02, 'ImmuneGravity' }, { 0x04, 'ImmuneBind' }, { 0x08, 'ImmuneStun' },
    { 0x10, 'ImmuneSilence' }, { 0x20, 'ImmuneParalyze' }, { 0x40, 'ImmuneBlind' }, { 0x80, 'ImmuneSlow' },
    { 0x100, 'ImmunePoison' }, { 0x200, 'ImmuneElegy' }, { 0x400, 'ImmuneRequiem' }, { 0x800, 'ImmuneLightSleep' },
    { 0x1000, 'ImmuneDarkSleep' }, { 0x2000, 'ImmunePetrify' },
};

M.LABELS = {
    H2H = 'Hand-to-hand', ImmuneSleep = 'Sleep', ImmuneGravity = 'Gravity', ImmuneBind = 'Bind', ImmuneStun = 'Stun',
    ImmuneSilence = 'Silence', ImmuneParalyze = 'Paralyze', ImmuneBlind = 'Blind', ImmuneSlow = 'Slow',
    ImmunePoison = 'Poison', ImmuneElegy = 'Elegy', ImmuneRequiem = 'Requiem', ImmuneLightSleep = 'Light sleep',
    ImmuneDarkSleep = 'Dark sleep', ImmunePetrify = 'Petrify',
};

function M.Available()
    return ashita.fs.exists(MOBDB .. 'data');
end

function M.IconPath(name)
    return MOBDB .. 'icons\\' .. name .. '.png';
end

local function loadZone(zone)
    if zone == M.zone then return M.data; end
    M.zone, M.data = zone, nil;
    local path = string.format('%sdata\\%u.lua', MOBDB, zone);
    if not ashita.fs.exists(path) then return nil; end
    local chunk = loadfile(path);
    if chunk == nil then return nil; end
    local ok, result = pcall(chunk);
    if ok and type(result) == 'table' then M.data = result; end
    return M.data;
end

-- Returns { mods = { {name, potency}, ... } sorted weakest-to-strongest-resist, immune = { iconName, ... } }
-- for the entity, or nil when MobDB has nothing on it.
function M.Lookup(index, entity)
    if entity == nil or entity.ServerId == nil then return nil; end
    local zone = bit.band(bit.rshift(entity.ServerId, 12), 0xFFF);
    local data = loadZone(zone);
    if data == nil then return nil; end
    local mob = (data.Indices and data.Indices[index]) or (data.Names and data.Names[entity.Name]);
    if mob == nil then return nil; end
    return M.FromMobDB(mob);
end

function M.FromMobDB(mob)
    local out = { mods = {}, immune = {} };
    for _, name in ipairs(MOD_ORDER) do
        local potency = mob.Modifiers and mob.Modifiers[name];
        if potency ~= nil and math.abs(potency - 1) > 0.001 then
            out.mods[#out.mods + 1] = { name, potency };
        end
    end
    table.sort(out.mods, function(a, b) return a[2] > b[2]; end);
    for _, entry in ipairs(IMMUNITIES) do
        if bit.band(mob.Immunities or 0, entry[1]) ~= 0 then out.immune[#out.immune + 1] = entry[2]; end
    end
    if #out.mods == 0 and #out.immune == 0 then return nil; end
    return out;
end

function M.HandleZone()
    M.zone, M.data = nil, nil;
end

return M;
