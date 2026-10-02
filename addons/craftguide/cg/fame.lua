--[[
    craftguide - fame and NPC prices, as PhoenixXI computes them (scripts/globals/shop.lua).

    Each shop prices by one fame area. Your fame there (and, for nations, your fame with the
    other two nations) gives a price rank from 1 to 21:
        San d'Oria / Bastok / Windurst  floor((2400 + 4 x this nation - other two nations + 399) / 400)
        Selbina / Rabao                 floor((1200 + San d'Oria + Bastok - Windurst + 199) / 200)
        Norg                            floor((7200 + 12 x Norg - all three nations + 1199) / 1200)
        Jeuno and everywhere else       11
    Buying:  listed price x (111 - rank) / 100   (shops with no fame area charge the listed price)
    Selling: base price x (rank + 389) / 400     (rank 11 at any shop with no fame area)
    Fame is kept in points (0-2500); fame level n starts at the points below.
]]

local M = {};

M.AREAS = {
    { key = 'SANDORIA', label = 'San d\'Oria' },
    { key = 'BASTOK', label = 'Bastok' },
    { key = 'WINDURST', label = 'Windurst' },
    { key = 'NORG', label = 'Norg' },
};

M.AREA_LABELS = { SANDORIA = 'San d\'Oria', BASTOK = 'Bastok', WINDURST = 'Windurst', NORG = 'Norg',
    SELBINA_RABAO = 'Selbina/Rabao', JEUNO = 'Jeuno' };

-- Points where fame levels 1-9 start on PhoenixXI: the pre-2014 ("era") thresholds from
-- modules/era/lua/data/fame_rank_points.lua, at the server's 0-2500 scale. (Retail after the
-- 2014 relaxation, scripts/data/fame.lua, used 0/50/125/225/325/425/488/550/613.)
-- "10" stands for maxed-out fame.
M.LEVEL_POINTS = { 0, 200, 500, 900, 1300, 1700, 1950, 2200, 2450, 2500 };

function M.levelLabel(level)
    return level >= 10 and '9 (maxed)' or tostring(level);
end

local function clamp(rank) return math.max(1, math.min(21, rank)); end

-- fame = { SANDORIA = level, BASTOK = level, WINDURST = level, NORG = level }
function M.points(fame, key)
    local level = math.max(1, math.min(10, fame[key] or 1));
    return M.LEVEL_POINTS[level];
end

function M.rank(fame, area)
    local s, b, w = M.points(fame, 'SANDORIA'), M.points(fame, 'BASTOK'), M.points(fame, 'WINDURST');
    if area == 'SANDORIA' then return clamp(math.floor((2400 + s * 4 - (b + w) + 399) / 400)); end
    if area == 'BASTOK' then return clamp(math.floor((2400 + b * 4 - (s + w) + 399) / 400)); end
    if area == 'WINDURST' then return clamp(math.floor((2400 + w * 4 - (s + b) + 399) / 400)); end
    if area == 'SELBINA_RABAO' then return clamp(math.floor((1200 + s + b - w + 199) / 200)); end
    if area == 'NORG' then
        return clamp(math.floor((7200 + M.points(fame, 'NORG') * 12 - (s + b + w) + 1199) / 1200));
    end
    return 11;
end

-- What a shop charges you for an offer from data/prices.lua.
function M.buyPrice(fame, offer)
    if offer.fame == nil or offer.fame == '' then return offer.price; end
    return math.max(1, math.floor(offer.price * (111 - M.rank(fame, offer.fame)) / 100));
end

-- The best place to sell: the highest price rank you have anywhere (never below 11, which
-- any Jeuno or non-fame shop gives). Returns rank, area key.
function M.bestSellRank(fame)
    local best, where = 11, 'JEUNO';
    for _, area in ipairs({ 'SANDORIA', 'BASTOK', 'WINDURST', 'SELBINA_RABAO', 'NORG' }) do
        local r = M.rank(fame, area);
        if r > best then best, where = r, area; end
    end
    return best, where;
end

function M.sellPrice(base, rank)
    if base == nil or base <= 0 then return nil; end
    return math.max(1, math.floor(base * (rank + 389) / 400));
end

-- Percent change against the listed/base price, for display.
function M.buyPercent(rank) return 11 - rank; end          -- negative = discount
function M.sellPercent(rank) return (rank - 11) / 4; end   -- vs. base price

return M;
