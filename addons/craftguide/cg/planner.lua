--[[
    craftguide - turns the engine's per-level picks into a whole leveling plan for one craft.
    Pure Lua: the caller passes in what it knows about the player.

    player = {
        skills = { Cooking = 34, ... },   whole levels for every craft
        ranks  = { Cooking = 3, ... },    guild ranks
        tenths = 6,                       progress into the current level of this craft (0-9)
    }
    The plan assumes you take each rank test as you reach it, so guild-supply items unlock
    decade by decade (rank = level / 10).
]]

local E = require('cg.engine');
local F = require('cg.fame');

local M = {};

function M.options(cfg, data, prices)
    local crystals = {};
    for id, price in pairs(cfg.crystalPrices) do crystals[id] = price; end
    local fame = cfg.fame or {};
    local sellRank = F.bestSellRank(fame);
    return {
        useRegional = cfg.useRegional, sellResults = cfg.sellResults, modern = cfg.modern,
        allowKeyItems = cfg.allowKeyItems, customPrices = cfg.customPrices, crystalPrices = crystals,
        skills = {}, ranks = {},
        offerPrice = function (o) return F.buyPrice(fame, o); end,
        sellPrice = function (item) return F.sellPrice(prices.base[item], sellRank); end,
    };
end

-- Options and pricer as they would be with `craft` at `level`.
function M.at(base, player, craft, level)
    local opts = {};
    for k, v in pairs(base) do opts[k] = v; end
    opts.skills, opts.ranks = {}, {};
    for k, v in pairs(player.skills) do opts.skills[k] = v; end
    for k, v in pairs(player.ranks) do opts.ranks[k] = v; end
    opts.skills[craft] = level;
    opts.ranks[craft] = math.max(player.ranks[craft] or 0, math.floor(level / 10));
    return opts;
end

-- Returns segments, total gil.
function M.plan(data, prices, base, player, craft, target)
    local level = player.skills[craft] or 0;
    local segments, total = {}, 0;
    local from = level;
    while from < target do
        local to = math.min(target, (math.floor(from / 10) + 1) * 10);
        local opts = M.at(base, player, craft, from);
        local pricer = E.newPricer(data, prices, opts);
        local segs, cost = E.path(data, craft, from, to, pricer, opts, from == level and player.tenths or nil);
        total = total + cost;
        for _, s in ipairs(segs) do
            local last = segments[#segments];
            if last and last.to == s.from and ((last.gap and s.gap) or
                (not last.gap and not s.gap and last.option.recipe == s.option.recipe)) then
                last.to = s.to;
                if not s.gap then
                    last.synths = last.synths + s.synths;
                    last.cost = last.cost + s.cost;
                end
            else
                s.opts = opts;
                segments[#segments + 1] = s;
            end
        end
        from = to;
    end
    return segments, total;
end

-- Expected purchases for a segment: crystals and each ingredient, allowing for the 50% loss
-- on failed synths. Returns { { id, qty, each, source }, ... }.
function M.shopping(seg)
    local o = seg.option;
    local r = o.recipe;
    local list = { { id = r.crystal, qty = math.ceil(seg.synths), each = seg.opts.crystalPrices[r.crystal] or 0, source = { kind = 'crystal' } } };
    for _, ing in ipairs(r.ingredients) do
        local each, source = seg.pricer.cost(ing[1]);
        local used = seg.synths * ing[2] * (o.success + (1 - o.success) * 0.5);
        list[#list + 1] = { id = ing[1], qty = math.ceil(used), each = each, source = source };
    end
    return list;
end

return M;
