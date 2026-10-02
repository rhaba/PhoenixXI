--[[
    craftguide - the window: one tab per craft, Fishing, and Settings.
]]

require('common');
local imgui = require('imgui');
local E = require('cg.engine');
local P = require('cg.planner');
local F = require('cg.fame');
local ui = require('phxui');

local M = { open = { false }, selectTab = nil, errorReported = false };

local CRYSTALS = { { 4096, 'Fire' }, { 4097, 'Ice' }, { 4098, 'Wind' }, { 4099, 'Earth' },
    { 4100, 'Lightning' }, { 4101, 'Water' }, { 4102, 'Light' }, { 4103, 'Dark' } };

local C = ui.color;
local GOOD, WARN, BAD, DIM, GOLD = C.ok, C.warn, C.bad, C.dim, C.gold;

-- Per-session UI state.
local state = {
    plans = {},                -- craft -> { key, segments, total }
    altLevel = {},             -- craft -> { level } for the alternatives slider
    priceInputs = {},          -- item -> { value } while typing a custom price
    fishSearch = { '' },
    fishMine = { false },
    fishHideOther = { true },
    fishSort = 1,
};

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local nameCache = {};
function M.itemName(id)
    local name = nameCache[id];
    if name == nil then
        local ok, res = pcall(function () return AshitaCore:GetResourceManager():GetItemById(id); end);
        name = (ok and res ~= nil and res.Name ~= nil and res.Name[1]) or ('Item ' .. id);
        nameCache[id] = name;
    end
    return name;
end

local function gil(n)
    if n == nil then return '?'; end
    local s = tostring(math.floor(n + 0.5));
    local neg = s:sub(1, 1) == '-';
    if neg then s = s:sub(2); end
    s = s:reverse():gsub('(%d%d%d)', '%1,'):reverse():gsub('^,', '');
    return (neg and '-' or '') .. s .. 'g';
end

local function tooltip(text)
    if imgui.IsItemHovered() then imgui.SetTooltip(text); end
end

local function fameNote(source)
    if source.fame == nil or source.fame == '' then return ''; end
    return ', ' .. (F.AREA_LABELS[source.fame] or source.fame) .. ' fame prices';
end

local function describeSource(source)
    if source == nil then return 'no NPC sells this: farm it, buy it, or set your price'; end
    local k = source.kind;
    if k == 'crystal' then return 'crystal (your price in Settings)'; end
    if k == 'custom' then return 'your price'; end
    if k == 'craft' then return 'make it: ' .. source.recipe.name; end
    local where = source.npc .. ', ' .. source.zone;
    if k == 'shop' then
        local loc = (source.loc and source.loc ~= '') and source.loc or nil;
        local note = fameNote(source);
        if loc then return where .. ' (' .. loc .. note .. ')'; end
        if note ~= '' then return where .. ' (' .. note:sub(3) .. ')'; end
        return where;
    end
    if k == 'guild-supply' then
        local rank = source.rank or 0;
        return where .. ' (' .. source.craft .. ' guild supply' .. (rank > 0 and (', rank ' .. rank .. '+') or '') .. ')';
    end
    if k == 'regional' then return where .. ' (while ' .. source.nation .. ' holds ' .. source.region .. fameNote(source) .. ')'; end
    if k == 'guild-shop' then
        return where .. ' (guild shop ' .. (source.hours or '') ..
            ((source.holiday and source.holiday ~= '') and (', closed ' .. source.holiday) or '') .. ', price varies with stock)';
    end
    return where;
end

local function setCustomPrice(cfg, item, value)
    if value == nil then cfg.customPrices[item] = nil; else cfg.customPrices[item] = math.max(0, value); end
    M.dirty = true;
end

-- A small "your price" box for one item.
local function priceInput(cfg, item, width)
    local buf = state.priceInputs[item];
    if buf == nil then
        buf = { cfg.customPrices[item] or 0 };
        state.priceInputs[item] = buf;
    end
    imgui.PushItemWidth(width or 90);
    local changed = imgui.InputInt('##price' .. item, buf, 0, 0);
    imgui.PopItemWidth();
    if changed then setCustomPrice(cfg, item, buf[1] > 0 and buf[1] or nil); end
    return changed;
end

-- ---------------------------------------------------------------------------
-- Plans
-- ---------------------------------------------------------------------------
local function planKey(cfg, player, craft, target)
    local parts = { craft, target, player.tenths or 0, M.revision or 0 };
    for _, c in ipairs(E.CRAFTS) do
        parts[#parts + 1] = (player.skills[c] or 0) .. '/' .. (player.ranks[c] or 0);
    end
    return table.concat(parts, ':');
end

local function getPlan(ctx, craft, player, target)
    local key = planKey(ctx.cfg, player, craft, target);
    local plan = state.plans[craft];
    if plan == nil or plan.key ~= key then
        local base = P.options(ctx.cfg, ctx.data, ctx.prices);
        local segments, total = P.plan(ctx.data, ctx.prices, base, player, craft, target);
        plan = { key = key, segments = segments, total = total, base = base };
        state.plans[craft] = plan;
    end
    return plan;
end

-- ---------------------------------------------------------------------------
-- Craft tab
-- ---------------------------------------------------------------------------
local function renderRanks(ctx, craft, info)
    local cfg = ctx.cfg;
    cfg.ranksDone[craft] = cfg.ranksDone[craft] or {};
    local done = cfg.ranksDone[craft];
    if not imgui.CollapsingHeader('Rank tests##' .. craft) then return; end
    for _, t in ipairs(ctx.data.ranks[craft] or {}) do
        local key = tostring(t.rank);
        local passed = info.rank >= t.rank;
        local box = { passed or done[key] == true };
        if imgui.Checkbox(t.name .. ' (cap ' .. ((t.rank + 1) * 10) .. ')##rank' .. craft .. key, box) then
            if passed then
                box[1] = true;  -- the game says you have it
            else
                done[key] = box[1] or nil;
                M.settingsChanged = true;
            end
        end
        imgui.SameLine();
        local item = t.item and M.itemName(t.item) or '?';
        local text = 'skill ' .. t.skill .. '+, turn in ' .. item;
        if passed then
            imgui.TextColored(DIM, text);
        elseif info.level >= t.skill then
            imgui.TextColored(GOOD, text .. '   <- ready now');
        else
            imgui.Text(text);
        end
        if t.rank == 10 then
            imgui.SameLine();
            imgui.TextColored(DIM, '(?)');
            tooltip('Expert also needs the guild\'s key-item quest and a signed item before the test is accepted.');
        end
    end
end

local function renderShopping(ctx, seg)
    local o = seg.option;
    local r = o.recipe;
    local reqs = {};
    for c, lv in pairs(r.skills) do reqs[#reqs + 1] = c .. ' ' .. lv; end
    table.sort(reqs);
    imgui.TextColored(DIM, string.format('%s   success ~%d%%   ~%s per synth   result: %s x%d%s', table.concat(reqs, ', '),
        math.floor(o.success * 100 + 0.5), gil(o.attemptCost), M.itemName(r.result), r.qty,
        (seg.opts.sellPrice(r.result) and (' (NPC pays ' .. gil(seg.opts.sellPrice(r.result) * r.qty) .. ')') or ' (NPCs won\'t buy it)')));
    if r.keyItem then imgui.TextColored(WARN, 'Needs a guild key item (advanced technique).'); end
    if imgui.BeginTable('##shop' .. tostring(r.id) .. '_' .. seg.from, 4,
        bit.bor(ImGuiTableFlags_BordersInnerH, ImGuiTableFlags_RowBg, ImGuiTableFlags_SizingStretchProp)) then
        imgui.TableSetupColumn('Buy', ImGuiTableColumnFlags_WidthStretch, 1.4);
        imgui.TableSetupColumn('Qty', ImGuiTableColumnFlags_WidthStretch, 0.35);
        imgui.TableSetupColumn('Each', ImGuiTableColumnFlags_WidthStretch, 0.5);
        imgui.TableSetupColumn('Where', ImGuiTableColumnFlags_WidthStretch, 2.6);
        imgui.TableHeadersRow();
        for _, x in ipairs(P.shopping(seg)) do
            imgui.TableNextRow();
            imgui.TableNextColumn(); imgui.Text(M.itemName(x.id));
            imgui.TableNextColumn(); imgui.Text(tostring(x.qty));
            imgui.TableNextColumn(); imgui.Text(gil(x.each));
            imgui.TableNextColumn(); imgui.TextWrapped(describeSource(x.source));
        end
        imgui.EndTable();
    end
end

local function renderAlternatives(ctx, craft, info, player)
    local cfg = ctx.cfg;
    if not imgui.CollapsingHeader('Alternatives at a level##alt' .. craft) then return; end
    local lv = state.altLevel[craft];
    if lv == nil then lv = { math.min(info.level, 109) }; state.altLevel[craft] = lv; end
    imgui.PushItemWidth(200);
    imgui.SliderInt('Level##altlv' .. craft, lv, 0, 109);
    imgui.PopItemWidth();
    imgui.SameLine();
    if imgui.SmallButton('Mine##altme' .. craft) then lv[1] = info.level; end

    local opts = P.at(state.plans[craft] and state.plans[craft].base or P.options(cfg, ctx.data, ctx.prices), player, craft, lv[1]);
    local pricer = E.newPricer(ctx.data, ctx.prices, opts);
    local priced, unpriced = E.optionsAt(ctx.data, craft, lv[1], pricer, opts);

    imgui.TextColored(DIM, 'Recipes that give skill-ups at ' .. lv[1] .. ', cheapest first. Gil is per level gained.');
    if imgui.BeginTable('##alts' .. craft, 5, bit.bor(ImGuiTableFlags_BordersInnerH, ImGuiTableFlags_RowBg, ImGuiTableFlags_SizingStretchProp)) then
        imgui.TableSetupColumn('Recipe', ImGuiTableColumnFlags_WidthStretch, 1.4);
        imgui.TableSetupColumn('Lv', ImGuiTableColumnFlags_WidthStretch, 0.3);
        imgui.TableSetupColumn('Success', ImGuiTableColumnFlags_WidthStretch, 0.45);
        imgui.TableSetupColumn('Gil / level', ImGuiTableColumnFlags_WidthStretch, 0.6);
        imgui.TableSetupColumn('Needs', ImGuiTableColumnFlags_WidthStretch, 2.0);
        imgui.TableHeadersRow();
        for i = 1, math.min(8, #priced) do
            local o = priced[i];
            imgui.TableNextRow();
            imgui.TableNextColumn(); imgui.Text(o.recipe.name);
            imgui.TableNextColumn(); imgui.Text(tostring(o.level));
            imgui.TableNextColumn(); imgui.Text(math.floor(o.success * 100 + 0.5) .. '%');
            imgui.TableNextColumn(); imgui.Text(gil(o.costPerTenth * 10));
            imgui.TableNextColumn();
            local names = {};
            for _, ing in ipairs(o.recipe.ingredients) do names[#names + 1] = M.itemName(ing[1]) .. (ing[2] > 1 and (' x' .. ing[2]) or ''); end
            imgui.TextWrapped(table.concat(names, ', '));
        end
        for i = 1, math.min(10, #unpriced) do
            local o = unpriced[i];
            imgui.TableNextRow();
            imgui.TableNextColumn(); imgui.TextColored(WARN, o.recipe.name);
            tooltip('Needs ingredients no NPC sells. Type what they cost you (AH, bazaar, or 0 if you farm them) and this recipe joins the plan.');
            imgui.TableNextColumn(); imgui.Text(tostring(o.level));
            imgui.TableNextColumn(); imgui.Text(math.floor(o.success * 100 + 0.5) .. '%');
            imgui.TableNextColumn(); imgui.TextColored(DIM, 'farmed');
            imgui.TableNextColumn();
            for _, item in ipairs(o.missing) do
                imgui.Text(M.itemName(item) .. ':');
                imgui.SameLine();
                priceInput(cfg, item, 80);
            end
        end
        imgui.EndTable();
    end
    if #priced == 0 and #unpriced == 0 then imgui.TextColored(DIM, 'Nothing above this level gives skill-ups.'); end
end

local function renderCraft(ctx, craft)
    local cfg = ctx.cfg;
    local info = ctx.skill(craft);
    local player = ctx.player(craft);

    local cap = (info.rank + 1) * 10;
    if imgui.BeginTable('##hdr' .. craft, 4, ImGuiTableFlags_SizingStretchSame) then
        ui.stat('Skill', string.format('%d.%s', info.level, info.tenths and tostring(info.tenths) or 'x'), C.peach);
        if info.tenths == nil then ui.tooltip('The decimal shows after your next skill-up.'); end
        ui.stat('Rank', ctx.rankName(craft, info.rank));
        ui.stat('Cap', tostring(cap));
        local status, color = 'Leveling', C.secondary;
        if info.level >= cap - 2 and info.rank < 10 then
            status, color = info.capped and 'Capped: take the test' or 'Rank test available', info.capped and BAD or GOOD;
        end
        ui.stat('Status', status, color);
        imgui.EndTable();
    end

    local target = cfg.targets[craft] or 100;
    local buf = { target };
    imgui.PushItemWidth(200);
    if imgui.SliderInt('Target level##t' .. craft, buf, math.min(info.level + 1, 110), 110) then
        cfg.targets[craft] = buf[1];
        M.settingsChanged = true;
    end
    imgui.PopItemWidth();
    target = cfg.targets[craft] or 100;

    renderRanks(ctx, craft, info);

    if info.level >= target then
        imgui.TextColored(GOOD, 'You are at or above the target level.');
    else
        local plan = getPlan(ctx, craft, player, target);
        ui.section('Cheapest path ' .. info.level .. ' to ' .. target);
        imgui.SameLine();
        imgui.TextColored(GOLD, '~' .. gil(plan.total));
        tooltip('Expected gil with NPC prices, your crystal prices and any prices you typed in, ' ..
            (cfg.sellResults and 'minus what NPCs pay for the results.' or 'without selling the results.') ..
            '\nGaps (no NPC-priced recipe) are not counted.');
        for _, seg in ipairs(plan.segments) do
            local label = string.format('%3d - %-3d', seg.from, seg.to);
            if seg.gap then
                imgui.TextColored(BAD, label .. '  No recipe with NPC-sold ingredients. Check the alternatives below.');
            else
                local open = imgui.TreeNode(string.format('%s  %s (lv %d)   ~%d synths   ~%s##seg%s%d',
                    label, seg.option.recipe.name, seg.option.level, math.ceil(seg.synths), gil(seg.cost), craft, seg.from));
                if open then
                    renderShopping(ctx, seg);
                    imgui.TreePop();
                end
            end
        end
    end

    imgui.Spacing();
    renderAlternatives(ctx, craft, info, player);
end

-- ---------------------------------------------------------------------------
-- Fishing tab
-- ---------------------------------------------------------------------------
local FISH_SORTS = { 'Skill', 'NPC price', 'Name' };

local function renderFishing(ctx)
    local info = ctx.skill('Fishing');
    ui.labelValue('Fishing', string.format('%d.%s', info.level, info.tenths and tostring(info.tenths) or 'x'), C.peach);
    imgui.SameLine();
    imgui.TextColored(DIM, '   Skill is the level where a fish stops being hard to land; you can hook it sooner.');

    imgui.PushItemWidth(200);
    imgui.InputTextWithHint('##fishsearch', 'Search fish, zone or bait', state.fishSearch, 64);
    imgui.PopItemWidth();
    imgui.SameLine(); imgui.Checkbox('Up to my skill + 10', state.fishMine);
    imgui.SameLine(); imgui.Checkbox('Hide junk', state.fishHideOther);
    tooltip('Hide items that are not fish (Rusty Bucket and the like).');
    imgui.SameLine();
    imgui.PushItemWidth(110);
    if imgui.BeginCombo('Sort##fishsort', FISH_SORTS[state.fishSort]) then
        for i, label in ipairs(FISH_SORTS) do
            if imgui.Selectable(label, state.fishSort == i) then state.fishSort = i; end
        end
        imgui.EndCombo();
    end
    imgui.PopItemWidth();

    local sellRank, sellArea = F.bestSellRank(ctx.cfg.fame or {});
    local function price(f) return F.sellPrice(f.base, sellRank); end
    local q = state.fishSearch[1]:lower();
    local rows = {};
    for _, f in ipairs(ctx.fish) do
        local show = f.id ~= 65535 and not (state.fishHideOther[1] and f.notFish);
        if show and state.fishMine[1] and f.skill > info.level + 10 then show = false; end
        if show and q ~= '' then
            local hay = f.name:lower() .. ' ' .. table.concat(f.where, ' '):lower() .. ' ' .. table.concat(f.baits, ' '):lower();
            show = hay:find(q, 1, true) ~= nil;
        end
        if show then rows[#rows + 1] = f; end
    end
    table.sort(rows, function (a, b)
        if state.fishSort == 2 then
            if (price(a) or 0) ~= (price(b) or 0) then return (price(a) or 0) > (price(b) or 0); end
        elseif state.fishSort == 3 then
            return a.name < b.name;
        elseif a.skill ~= b.skill then
            return a.skill < b.skill;
        end
        return a.name < b.name;
    end);

    if imgui.BeginTable('##fish', 5, bit.bor(ImGuiTableFlags_Borders, ImGuiTableFlags_RowBg, ImGuiTableFlags_SizingStretchProp)) then
        imgui.TableSetupColumn('Fish', ImGuiTableColumnFlags_WidthStretch, 1.1);
        imgui.TableSetupColumn('Skill', ImGuiTableColumnFlags_WidthStretch, 0.3);
        imgui.TableSetupColumn('NPC price', ImGuiTableColumnFlags_WidthStretch, 0.5);
        imgui.TableSetupColumn('Where', ImGuiTableColumnFlags_WidthStretch, 2.4);
        imgui.TableSetupColumn('Best bait', ImGuiTableColumnFlags_WidthStretch, 1.2);
        imgui.TableHeadersRow();
        for _, f in ipairs(rows) do
            imgui.TableNextRow();
            imgui.TableNextColumn();
            local color = f.skill <= info.level and GOOD or (f.skill <= info.level + 10 and WARN or nil);
            if color then imgui.TextColored(color, f.name); else imgui.Text(f.name); end
            if f.legendary or f.quest then
                imgui.SameLine(); imgui.TextColored(DIM, f.legendary and '(legendary)' or '(quest)');
            end
            imgui.TableNextColumn(); imgui.Text(tostring(f.skill));
            imgui.TableNextColumn();
            local p = price(f);
            if p then imgui.TextColored(GOLD, gil(p)); else imgui.TextColored(C.faint, 'no sale'); end
            imgui.TableNextColumn();
            local shown = {};
            for i = 1, math.min(3, #f.where) do shown[i] = f.where[i]; end
            local text = table.concat(shown, '; ');
            if #f.where > 3 then text = text .. ' (+' .. (#f.where - 3) .. ' more)'; end
            imgui.TextWrapped(text);
            if #f.where > 3 then tooltip(table.concat(f.where, '\n')); end
            imgui.TableNextColumn(); imgui.TextWrapped(table.concat(f.baits, ', '));
        end
        imgui.EndTable();
    end
    imgui.TextColored(DIM, string.format('%d shown. Prices are what the best-paying NPC pays for your fame: %s (price rank %d). Set fame in Settings.',
        #rows, sellArea == 'JEUNO' and 'Jeuno or any shop without fame pricing' or (F.AREA_LABELS[sellArea] .. ' shops'), sellRank));
end

-- ---------------------------------------------------------------------------
-- Settings tab
-- ---------------------------------------------------------------------------
local function checkbox(cfg, label, key, help)
    local buf = { cfg[key] == true };
    if imgui.Checkbox(label, buf) then
        cfg[key] = buf[1];
        M.dirty = true;
        M.settingsChanged = true;
    end
    if help then tooltip(help); end
end

local function renderSettings(ctx)
    local cfg = ctx.cfg;
    ui.section('Theme');
    if ui.themeCombo('Window theme##cg_theme', cfg.theme) then
        cfg.theme = ui.theme;
        M.settingsChanged = true;
    end
    ui.tooltip('Phoenix (ember red), Farplane (FFX ember and mist), Farplane9 (Farplane colors, squared FF9 style; the default), Umbrella (Resident Evil green),\nMidnight (blue) or Classic (stock ImGui).');

    ui.section('Plan');
    checkbox(cfg, 'Use regional vendors', 'useRegional', 'Regional produce vendors only sell while their nation holds the region (conquest).');
    checkbox(cfg, 'Sell results to NPCs', 'sellResults', 'Subtract what an NPC pays for each result (fame level 1).');
    checkbox(cfg, 'Include recipes that need a guild key item', 'allowKeyItems');
    checkbox(cfg, 'Modern skill-up rules', 'modern', 'Off: era rules, where only recipes above your skill give skill-ups (PhoenixXI default).\nOn: the newer rules, where recipes up to 10 levels below you still can.');

    ui.section('Fame');
    imgui.TextColored(DIM, 'Fame moves NPC prices by up to 10%. Ask a fame NPC in each city for your level.');
    cfg.fame = cfg.fame or {};
    for _, a in ipairs(F.AREAS) do
        local level = cfg.fame[a.key] or 1;
        imgui.PushItemWidth(110);
        if imgui.BeginCombo(a.label .. '##fame' .. a.key, F.levelLabel(level)) then
            for lv = 1, 10 do
                if imgui.Selectable(F.levelLabel(lv), lv == level) then
                    cfg.fame[a.key] = lv;
                    M.dirty = true;
                end
            end
            imgui.EndCombo();
        end
        imgui.PopItemWidth();
        local rank = F.rank(cfg.fame, a.key);
        imgui.SameLine(260);
        imgui.TextColored(DIM, string.format('price rank %d: buy %+d%%, sell %+.2f%%', rank, F.buyPercent(rank), F.sellPercent(rank)));
    end
    local sr = F.rank(cfg.fame, 'SELBINA_RABAO');
    imgui.TextColored(DIM, string.format('Selbina/Rabao (from nation fame): price rank %d: buy %+d%%, sell %+.2f%%', sr, F.buyPercent(sr), F.sellPercent(sr)));
    imgui.TextColored(DIM, 'Jeuno and shops without fame pricing: rank 11, the listed price and full base sell price.');
    tooltip('Fame with one nation lowers your price rank with the other two nations\' shops.\nEach level counts as just reaching it; "9 (maxed)" is the top of the fame scale.');

    ui.section('Crystal prices (each)');
    imgui.TextColored(DIM, 'NPCs don\'t sell crystals, so set what they cost you. 0 if you farm them.');
    for i, c in ipairs(CRYSTALS) do
        local buf = { cfg.crystalPrices[c[1]] or 0 };
        imgui.PushItemWidth(90);
        if imgui.InputInt(c[2] .. '##crystal' .. c[1], buf, 0, 0) then
            cfg.crystalPrices[c[1]] = math.max(0, buf[1]);
            M.dirty = true;
            M.settingsChanged = true;
        end
        imgui.PopItemWidth();
        if i % 4 ~= 0 then imgui.SameLine(); end
    end

    ui.section('Your item prices');
    local items = {};
    for id in pairs(cfg.customPrices) do items[#items + 1] = id; end
    table.sort(items, function (a, b) return M.itemName(a) < M.itemName(b); end);
    if #items == 0 then
        imgui.TextColored(DIM, 'None yet. Add them from a craft\'s Alternatives list.');
    end
    for _, id in ipairs(items) do
        imgui.Text(M.itemName(id));
        imgui.SameLine(260);
        priceInput(cfg, id, 90);
        imgui.SameLine();
        if imgui.SmallButton('Remove##rm' .. id) then
            setCustomPrice(cfg, id, nil);
            state.priceInputs[id] = nil;
        end
    end

    ui.section('Data');
    imgui.TextColored(DIM, 'PhoenixXI server (recipes, guild shops, regional vendors, fishing, item prices);');
    imgui.TextColored(DIM, 'regular NPC shop list from VanaCompass - Phoenix. Nothing is read from other addons at runtime.');
end

-- ---------------------------------------------------------------------------
function M.Render(ctx)
    if not M.open[1] then return; end
    imgui.SetNextWindowSize({ 760, 560 }, ImGuiCond_FirstUseEver);
    local token = ui.push();
    if imgui.Begin('Craft Guide##craftguide', M.open, ImGuiWindowFlags_NoCollapse) then
        local ok, err = pcall(function ()
            if imgui.BeginTabBar('##craftguide_tabs') then
                for _, craft in ipairs(E.CRAFTS) do
                    local flags = (M.selectTab == craft) and ImGuiTabItemFlags_SetSelected or ImGuiTabItemFlags_None;
                    if imgui.BeginTabItem(craft, nil, flags) then
                        renderCraft(ctx, craft);
                        imgui.EndTabItem();
                    end
                end
                local flags = (M.selectTab == 'Fishing') and ImGuiTabItemFlags_SetSelected or ImGuiTabItemFlags_None;
                if imgui.BeginTabItem('Fishing', nil, flags) then
                    renderFishing(ctx);
                    imgui.EndTabItem();
                end
                if imgui.BeginTabItem('Settings') then
                    renderSettings(ctx);
                    imgui.EndTabItem();
                end
                M.selectTab = nil;
                imgui.EndTabBar();
            end
        end);
        if not ok and not M.errorReported then
            M.errorReported = true;
            ctx.err('Window error: ' .. tostring(err));
        end
    end
    imgui.End();
    ui.pop(token);
    if M.dirty then
        M.dirty = false;
        M.revision = (M.revision or 0) + 1;
        M.settingsChanged = true;
    end
end

return M;
