--[[
    craftguide for Ashita v4 - PhoenixXI

    The cheapest way to level every craft with NPC-bought materials, rank test checklist,
    craft skill tracking (with decimals once you skill up), and a fishing list with NPC
    prices, catch spots, skill levels and baits.

    Standalone: everything it needs ships in data/. Data credits are in README.md.
]]

addon.name    = 'craftguide';
addon.author  = 'Spongeh';
addon.version = '1.3.3';
addon.desc    = 'Cheapest NPC-material crafting paths, rank tests and a fishing guide for PhoenixXI.';
addon.link    = '';

require('common');
local chat = require('chat');
local settingsLib = require('settings');

local E = require('cg.engine');
local ui = require('cg.ui');

local data = require('data.crafts');
local prices = require('data.prices');
local fish = require('data.fishing');

-- GetCraftSkill index and battle-message skill id (48 + index) for each craft.
local CRAFT_INDEX = { Fishing = 0, Woodworking = 1, Smithing = 2, Goldsmithing = 3, Clothcraft = 4,
    Leathercraft = 5, Bonecraft = 6, Alchemy = 7, Cooking = 8 };
local CRAFT_BY_SKILL = {};
for name, idx in pairs(CRAFT_INDEX) do CRAFT_BY_SKILL[48 + idx] = name; end

local RANK_NAMES = { [0] = 'Amateur', 'Recruit', 'Initiate', 'Novice', 'Apprentice', 'Journeyman',
    'Craftsman', 'Artisan', 'Adept', 'Veteran', 'Expert', 'Authority' };

local defaults = T{
    crystalPrices = T{ [4096] = 150, [4097] = 150, [4098] = 150, [4099] = 150,
        [4100] = 150, [4101] = 150, [4102] = 150, [4103] = 150 },
    customPrices = T{},
    useRegional = true,
    sellResults = true,
    allowKeyItems = false,
    modern = false,
    theme = 'Farplane9',    -- window theme (phxui): Phoenix, Farplane, Umbrella, Midnight, Classic
    targets = T{},
    -- Fame level (1-9, 10 = maxed) per area; shop prices follow it.
    fame = T{ SANDORIA = 1, BASTOK = 1, WINDURST = 1, NORG = 1 },
    ranksDone = T{},
    -- Last known skill in tenths per craft, so the decimal survives a reload.
    tenths = T{},
};

local cfg = settingsLib.load(defaults);
local phx = require('phxui');
if phx.adoptPackTheme(cfg, 'theme') then settingsLib.save(); end
phx.setTheme(cfg.theme);

settingsLib.register('settings', 'craftguide_settings_update', function (s)
    if s ~= nil then
        cfg = s;
        if phx.adoptPackTheme(cfg, 'theme') then settingsLib.save(); end
        phx.setTheme(cfg.theme);
        ui.revision = (ui.revision or 0) + 1;
    end
end);

local function msg(text) print(chat.header(addon.name):append(chat.message(text))); end
local function err(text) print(chat.header(addon.name):append(chat.error(text))); end

-- ---------------------------------------------------------------------------
-- Skills
-- ---------------------------------------------------------------------------
local function readSkill(craft)
    local ok, s = pcall(function ()
        return AshitaCore:GetMemoryManager():GetPlayer():GetCraftSkill(CRAFT_INDEX[craft]);
    end);
    if not ok or s == nil then return { level = 0, rank = 0, capped = false }; end
    local info = { level = s:GetSkill(), rank = s:GetRank(), capped = s:IsCapped() };
    -- The decimal is only known from skill-up messages; keep it while the level still matches.
    local t = cfg.tenths[craft];
    if t ~= nil and math.floor(t / 10) == info.level then info.tenths = t % 10; end
    return info;
end

local function playerFor(craft)
    local player = { skills = {}, ranks = {}, tenths = nil };
    for _, c in ipairs(E.CRAFTS) do
        local s = readSkill(c);
        player.skills[c], player.ranks[c] = s.level, s.rank;
    end
    player.tenths = readSkill(craft).tenths;
    return player;
end

local ctx = {
    data = data, prices = prices, fish = fish, err = err,
    skill = readSkill, player = playerFor,
    rankName = function (_, rank) return RANK_NAMES[rank] or ('Rank ' .. rank); end,
};

-- ---------------------------------------------------------------------------
-- Events
-- ---------------------------------------------------------------------------
ashita.events.register('packet_in', 'craftguide_packet_in', function (e)
    if e.id ~= 0x029 then return; end
    local message = struct.unpack('H', e.data, 0x18 + 1);
    if message ~= 38 and message ~= 53 and message ~= 310 then return; end
    local skillId = struct.unpack('I', e.data, 0x0C + 1);
    local craft = CRAFT_BY_SKILL[skillId];
    if craft == nil then return; end
    local value = struct.unpack('I', e.data, 0x10 + 1);

    local level = readSkill(craft).level;
    local tenths = cfg.tenths[craft];
    if message == 38 then
        -- Skill rises by value tenths. Memory may still show the old level this instant.
        if tenths == nil or math.floor(tenths / 10) ~= level then tenths = level * 10; end
        tenths = tenths + value;
    elseif message == 53 then
        -- Level up: value is the new level. Keep the decimal we've been counting if it agrees.
        if tenths == nil or math.floor(tenths / 10) ~= value then tenths = value * 10; end
    else
        -- Skill drop (synergy/cap rules): we no longer know the decimal.
        tenths = nil;
    end
    cfg.tenths[craft] = tenths;
    settingsLib.save();
end);

ashita.events.register('d3d_present', 'craftguide_present', function ()
    ctx.cfg = cfg;
    ui.Render(ctx);
    if ui.settingsChanged then
        ui.settingsChanged = false;
        settingsLib.save();
    end
end);

ashita.events.register('unload', 'craftguide_unload', function ()
    settingsLib.save();
end);

-- ---------------------------------------------------------------------------
-- Commands: /craftguide or /cg
-- ---------------------------------------------------------------------------
local TAB_ALIASES = {
    wood = 'Woodworking', woodworking = 'Woodworking', ww = 'Woodworking',
    smith = 'Smithing', smithing = 'Smithing', sm = 'Smithing',
    gold = 'Goldsmithing', goldsmithing = 'Goldsmithing', gs = 'Goldsmithing',
    cloth = 'Clothcraft', clothcraft = 'Clothcraft', cl = 'Clothcraft',
    leather = 'Leathercraft', leathercraft = 'Leathercraft', lth = 'Leathercraft',
    bone = 'Bonecraft', bonecraft = 'Bonecraft', bn = 'Bonecraft',
    alchemy = 'Alchemy', alc = 'Alchemy',
    cooking = 'Cooking', cook = 'Cooking', ck = 'Cooking',
    fishing = 'Fishing', fish = 'Fishing',
};

ashita.events.register('command', 'craftguide_command', function (e)
    local args = e.command:args();
    if #args == 0 then return; end
    local cmd = args[1]:lower();
    if cmd ~= '/craftguide' and cmd ~= '/cg' then return; end
    e.blocked = true;

    local sub = (args[2] or ''):lower();
    if sub == '' then
        ui.open[1] = not ui.open[1];
    elseif TAB_ALIASES[sub] then
        ui.open[1] = true;
        ui.selectTab = TAB_ALIASES[sub];
    elseif sub == 'theme' then
        local name = phx.findTheme(args[3]);
        if name == nil then
            msg('Themes: ' .. table.concat(phx.THEMES, ', ') .. '. Use /cg theme <name>.');
        else
            cfg.theme = phx.setTheme(name);
            settingsLib.save();
            msg('Theme: ' .. name);
        end
    elseif sub == 'skills' then
        for _, c in ipairs({ 'Woodworking', 'Smithing', 'Goldsmithing', 'Clothcraft', 'Leathercraft', 'Bonecraft', 'Alchemy', 'Cooking', 'Fishing' }) do
            local s = readSkill(c);
            msg(string.format('%-13s %d.%s  %s', c, s.level, s.tenths and tostring(s.tenths) or '?', RANK_NAMES[s.rank] or ''));
        end
    else
        msg('/cg - open or close the window');
        msg('/cg <craft|fishing> - open on that tab (cook, smith, gold, cloth, leather, bone, alchemy, wood, fish)');
        msg('/cg skills - print your craft skills and ranks');
        msg('/cg theme <name> - window theme: ' .. table.concat(phx.THEMES, ', '));
    end
end);
