--[[
    craftguide - the cheapest-path engine. Pure Lua (no Ashita calls) so it can be tested alone.

    Models LandSandBoat's synthesis rules as PhoenixXI runs them (src/map/utils/synthutils.cpp):
      difficulty d = recipe level - your whole skill level
      success     95% when d <= 0, 95 - 5d for d = 1..3, 80 - 10(d - 3) beyond (per craft involved)
      skill-up    era rules: only when d > 0; 60% below skill 50.0, 25% from 50.0 on.
                  modern rules (optional): possible down to d = -10, chance grows with d.
                  A failed synth can still skill up when 0 < d <= 5, at half the chance.
      amount      0.1-0.5 by d below skill 60.0 (weighted table), always 0.1 from 60.0 on.
      on fail     each ingredient is lost with 50% chance; the crystal is always used.

    Expected cost per 0.1 skill =
        (crystal + success x (ingredients - NPC value of the result) + fail x 50% of ingredients)
        / (expected skill gained per attempt)
    and each whole level takes 10 of those, using the cheapest recipe at that level.
]]

local M = {};

M.CRAFTS = { 'Woodworking', 'Smithing', 'Goldsmithing', 'Clothcraft', 'Leathercraft', 'Bonecraft', 'Alchemy', 'Cooking' };

-- Skill-up amount weights (tenths 1..5) by difficulty, from synthutils.cpp.
local AMOUNT_WEIGHTS = {
    [0] = { 85, 15, 0, 0, 0 }, { 85, 15, 0, 0, 0 }, { 85, 15, 0, 0, 0 }, { 80, 20, 0, 0, 0 }, { 80, 20, 0, 0, 0 },
    { 70, 30, 0, 0, 0 }, { 70, 30, 0, 0, 0 }, { 60, 40, 0, 0, 0 }, { 60, 40, 0, 0, 0 }, { 50, 40, 10, 0, 0 },
    { 40, 40, 20, 0, 0 }, { 40, 40, 20, 0, 0 }, { 15, 45, 30, 10, 0 }, { 10, 40, 25, 25, 0 }, { 0, 40, 30, 20, 10 },
};

-- Ingredients the server never loses on a failed synth (100% kept) or always loses.
local LOSS_RATES = { [489] = 0, [9091] = 0, [722] = 1, [860] = 1, [1288] = 1, [1289] = 1, [1290] = 1,
    [1291] = 1, [1292] = 1, [1409] = 1, [2169] = 1 };

function M.successRate(d)
    local rate;
    if d <= 0 then rate = 95;
    elseif d <= 3 then rate = 95 - 5 * d;
    else rate = 80 - 10 * (d - 3); end
    return math.max(0, math.min(99, rate)) / 100;
end

local function expectedAmount(d, skillTenths)
    if skillTenths >= 600 then return 1; end
    local w = AMOUNT_WEIGHTS[math.max(0, math.min(14, d))];
    return (w[1] * 1 + w[2] * 2 + w[3] * 3 + w[4] * 4 + w[5] * 5) / 100;
end

-- Chance per attempt of a skill-up at whole level `level` (tenths = level * 10).
local function skillUpChance(d, level, modern)
    local tenths = level * 10;
    if modern then
        if d <= -11 then return 0; end
        local base = 3.0 - math.log(1.2 + tenths / 100);
        if d > 1 then return d * base / 5; end
        return base / (6 - d);
    end
    if d <= 0 then return 0; end
    return (tenths < 500) and 0.6 or 0.25;
end

-- Expected tenths gained per attempt.
function M.expectedGain(d, level, modern)
    local c = skillUpChance(d, level, modern);
    if c <= 0 then return 0; end
    local p = M.successRate(d);
    local onFail = (d > 0 and d <= 5) and (c / 2) or 0;
    return (p * c + (1 - p) * onFail) * expectedAmount(d, level * 10);
end

-- ---------------------------------------------------------------------------
-- Item costs
-- ---------------------------------------------------------------------------
-- Cheapest way to get one of an item: an NPC offer the player can use, a price they typed in,
-- or (for intermediates) making it from priced ingredients with a recipe their skills already
-- handle at full success. Returns cost, source table.
function M.newPricer(data, prices, opts)
    local pricer = { cache = {}, making = {} };

    local function offerUsable(o)
        if o.kind == 'regional' and not opts.useRegional then return false; end
        if o.kind == 'guild-supply' then
            local rank = opts.ranks[o.craft] or 0;
            if rank < (o.rank or 0) then return false; end
        end
        return true;
    end

    -- Recipes that produce each item, for intermediates.
    local producers = {};
    for _, r in ipairs(data.recipes) do
        producers[r.result] = producers[r.result] or {};
        table.insert(producers[r.result], r);
    end

    local function canMakeSafely(r)
        if r.keyItem then return false; end
        for craft, level in pairs(r.skills) do
            if (opts.skills[craft] or 0) < level then return false; end
        end
        return true;
    end

    function pricer.cost(item, depth)
        depth = depth or 0;
        local cached = pricer.cache[item];
        if cached ~= nil then return cached.cost, cached.source; end

        local best, source = nil, nil;
        local custom = opts.customPrices[item];
        if custom ~= nil then best, source = custom, { kind = 'custom' }; end
        for _, o in ipairs((prices.buy[item]) or {}) do
            if offerUsable(o) then
                local price = opts.offerPrice and opts.offerPrice(o) or o.price;
                if best == nil or price < best then best, source = price, o; end
            end
        end
        if depth < 3 and not pricer.making[item] then
            pricer.making[item] = true;
            for _, r in ipairs(producers[item] or {}) do
                if canMakeSafely(r) then
                    local total = opts.crystalPrices[r.crystal] or 0;
                    local ok = true;
                    for _, ing in ipairs(r.ingredients) do
                        local c = pricer.cost(ing[1], depth + 1);
                        if c == nil then ok = false; break; end
                        total = total + c * ing[2];
                    end
                    if ok then
                        local each = total / math.max(1, r.qty);
                        if best == nil or each < best then best, source = each, { kind = 'craft', recipe = r }; end
                    end
                end
            end
            pricer.making[item] = nil;
        end
        pricer.cache[item] = { cost = best, source = source };
        return best, source;
    end

    return pricer;
end

-- ---------------------------------------------------------------------------
-- Recipe evaluation at one level
-- ---------------------------------------------------------------------------
-- Returns { recipe, costPerTenth, gain, attemptCost, synthsPerLevel, missing = {items} } or nil.
function M.evaluate(r, craft, level, pricer, opts)
    local recipeLevel = r.skills[craft];
    if recipeLevel == nil then return nil; end
    if r.keyItem and not opts.allowKeyItems then return nil; end
    local d = recipeLevel - level;
    local gain = M.expectedGain(d, level, opts.modern);
    if gain <= 0 then return nil; end

    -- Other crafts the recipe needs: their success rolls stack.
    local success = M.successRate(d);
    for other, need in pairs(r.skills) do
        if other ~= craft then
            local have = opts.skills[other] or 0;
            if need - have > 0 and not opts.ignoreSubCrafts then
                success = success * M.successRate(need - have);
            end
        end
    end

    local ingredients, kept, missing = 0, 0, {};
    for _, ing in ipairs(r.ingredients) do
        local c = pricer.cost(ing[1]);
        if c == nil then
            missing[#missing + 1] = ing[1];
        else
            ingredients = ingredients + c * ing[2];
            local loss = LOSS_RATES[ing[1]] or 0.5;
            kept = kept + c * ing[2] * (1 - loss);
        end
    end
    local crystal = opts.crystalPrices[r.crystal] or 0;
    local resultValue = opts.sellResults and ((opts.sellPrice(r.result) or 0) * r.qty) or 0;
    local attempt = crystal + success * (ingredients - resultValue) + (1 - success) * (ingredients - kept);
    return {
        recipe = r, level = recipeLevel, d = d, success = success, gain = gain,
        attemptCost = attempt, costPerTenth = attempt / gain, synthsPerLevel = 10 / gain,
        missing = missing,
    };
end

-- Recipes that use a craft, built once per data table.
function M.byCraft(data, craft)
    data._byCraft = data._byCraft or {};
    local list = data._byCraft[craft];
    if list == nil then
        list = {};
        for _, r in ipairs(data.recipes) do
            if r.skills[craft] ~= nil then list[#list + 1] = r; end
        end
        data._byCraft[craft] = list;
    end
    return list;
end

-- Every recipe that gives skill-ups at this level, cheapest first. `missing` entries can't be
-- priced (farmed / AH ingredients) and sort after the priced ones.
function M.optionsAt(data, craft, level, pricer, opts)
    local priced, unpriced = {}, {};
    for _, r in ipairs(M.byCraft(data, craft)) do
        local e = M.evaluate(r, craft, level, pricer, opts);
        if e then
            if #e.missing == 0 then priced[#priced + 1] = e; else unpriced[#unpriced + 1] = e; end
        end
    end
    table.sort(priced, function(a, b) return a.costPerTenth < b.costPerTenth; end);
    -- Unpriced: closest to the level band first (they're shown as alternatives).
    table.sort(unpriced, function(a, b)
        if #a.missing ~= #b.missing then return #a.missing < #b.missing; end
        return a.d < b.d;
    end);
    return priced, unpriced;
end

-- ---------------------------------------------------------------------------
-- The path
-- ---------------------------------------------------------------------------
-- Level by level from `from` to `to`, merged into segments that use the same recipe.
-- Each segment: { from, to, option, synths, cost } or { from, to, gap = true } when nothing
-- priceable gives skill-ups there. `startTenths` (0-9) is progress already made into `from`.
function M.path(data, craft, from, to, pricer, opts, startTenths)
    local segments = {};
    local total = 0;
    for level = from, to - 1 do
        local priced = M.optionsAt(data, craft, level, pricer, opts);
        local best = priced[1];
        local last = segments[#segments];
        if best == nil then
            if last and last.gap then last.to = level + 1;
            else segments[#segments + 1] = { from = level, to = level + 1, gap = true }; end
        else
            local share = (level == from and startTenths) and ((10 - startTenths) / 10) or 1;
            local cost = best.costPerTenth * 10 * share;
            local synths = best.synthsPerLevel * share;
            total = total + cost;
            if last and not last.gap and last.option.recipe == best.recipe then
                last.to = level + 1;
                last.synths = last.synths + synths;
                last.cost = last.cost + cost;
            else
                segments[#segments + 1] = { from = level, to = level + 1, option = best, synths = synths, cost = cost, pricer = pricer };
            end
        end
    end
    return segments, total;
end

return M;
