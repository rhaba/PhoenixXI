--[[
    enemybar - draws every bar. Each frame (target, target of target, sub-target, focus,
    aggro stack) is its own borderless ImGui window, so each can be dragged on its own in
    setup mode and is pinned in place otherwise.

    Bar layout and textures follow enemybar2 (Copyright (c) 2015, Mike McKee; BSD 3-Clause,
    see NOTICE.md): textured body with end caps, "Name: HP%" inside the bar, distance on the
    left, the current action above the right end, and an optional arrow + target-of-target
    name on the right. Status icons and timers use XIUI's statusicons (GPL-3).
]]

require('common');
local imgui = require('imgui');
local ekg = require('eb.ekg');
local hearts = require('eb.hearts');
local ff9 = require('eb.ff9');
local util = require('eb.util');
local tracker = require('eb.tracker');
local TextureManager = require('libs.texturemanager');
local imtext = require('libs.imtext');
local statusIcons = require('libs.statusicons');
local statusHandler = require('handlers.statushandler');
local buffTable = require('libs.bufftable');
local debuffHandler = require('handlers.debuffhandler');
local enemyCasts = require('handlers.enemycasts');
local resists = require('eb.resists');
local ui = require('phxui');

local M = {
    setup = false,     -- drag mode with demo data
    forcePos = {},     -- [frameKey] = true: re-apply the saved position next frame
    pending = {},      -- [frameKey] = { x, y } moved by dragging, saved on mouse release
};

local WHITE = { 1, 1, 1, 1 };
local WEAK_COLOR = { 0.45, 0.92, 0.45, 1 };     -- takes more damage
local RESIST_COLOR = { 1.0, 0.45, 0.45, 1 };    -- takes less damage
local MARKER_COLORS = { [1] = util.rgb255(255, 100, 100), [2] = util.rgb255(100, 100, 255) };

-- Hover tooltips (resist icons) in the shared phxui look. The bar windows push zero padding,
-- so give the tooltip its own padding and rounding. Push/pop are balanced in one call.
local function themedTooltip(text)
    local c = ui.color;
    imgui.PushStyleColor(ImGuiCol_PopupBg, c.surface1);
    imgui.PushStyleColor(ImGuiCol_Border, c.border);
    imgui.PushStyleColor(ImGuiCol_Text, c.text);
    imgui.PushStyleVar(ImGuiStyleVar_WindowPadding, { 8, 6 });
    imgui.PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0);
    imgui.SetTooltip(text);
    imgui.PopStyleVar(2);
    imgui.PopStyleColor(3);
end

local BASE_FLAGS =bit.bor(ImGuiWindowFlags_NoTitleBar, ImGuiWindowFlags_NoResize, ImGuiWindowFlags_NoScrollbar,
    ImGuiWindowFlags_NoCollapse, ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoFocusOnAppearing,
    ImGuiWindowFlags_NoNav, ImGuiWindowFlags_NoSavedSettings);

-- ---------------------------------------------------------------------------
-- Data for one bar: live from the game, or demo values in setup mode
-- ---------------------------------------------------------------------------
local function copyArray(t)
    local out = {};
    for i, v in ipairs(t or {}) do out[i] = v; end
    return out;
end

local function copyMap(t)
    local out = {};
    for k, v in pairs(t or {}) do out[k] = v; end
    return out;
end

-- XIUI returns reused tables, so copy before the next call overwrites them.
local function debuffsFor(serverId)
    local ids, times, uncertain = debuffHandler.GetActiveDebuffs(serverId);
    if ids == nil or #ids == 0 then return nil; end
    local orderedIds, orderedTimes = statusIcons.ReorderDebuffsFirst(ids, buffTable, times);
    return { ids = copyArray(orderedIds), times = copyArray(orderedTimes), uncertain = copyMap(uncertain) };
end

local function targetMarker(index, mainIdx, subIdx)
    if index == nil then return nil; end
    if index == mainIdx then return 1; end
    if index == subIdx then return 2; end
    return nil;
end

local function liveView(index, mainIdx, subIdx, extra)
    local e = util.entity(index);
    if e == nil then return nil; end
    local v = {
        index = index, name = e.Name, hpp = e.HPPercent, dist = util.distance(e),
        claim = tracker.ClaimState(e, index), marker = targetMarker(index, mainIdx, subIdx),
    };
    local active, progress, overlay, spellName = enemyCasts.GetRenderState(e.ServerId);
    if active and spellName ~= nil then
        v.action = { name = spellName, progress = progress or 0, interrupted = overlay ~= nil };
    end
    local totIndex = (extra and extra.totIndex) or tracker.TargetOf(index);
    local te = totIndex and util.entity(totIndex);
    if te ~= nil then v.tot = { name = te.Name, claim = tracker.ClaimState(te, totIndex) }; end
    v.debuffs = debuffsFor(e.ServerId);
    v.resists = resists.Lookup(index, e);
    return v;
end

local DEMO_DEBUFFS = { ids = { 2, 3, 4, 5, 6 }, times = { 45, 120, 8, 210, 30 }, uncertain = { [4] = true } };
local DEMO_RESISTS = { mods = { { 'Fire', 1.25 }, { 'Light', 1.25 }, { 'Water', 0.5 }, { 'Dark', 0.5 } },
    immune = { 'ImmuneSleep', 'ImmuneStun' } };

local function demoView(key, row)
    local base = {
        target    = { name = 'Target Name', hpp = 79, dist = 12.1, marker = 1 },
        tot       = { name = 'Target of Target', hpp = 92, dist = 4.2, claim = 'member' },
        subtarget = { name = 'Subtarget Name', hpp = 53, dist = 11.4, marker = 2 },
        focus     = { name = 'Focus Target Name', hpp = 36, dist = 8.6 },
        aggro     = { name = 'Aggro Target ' .. tostring(row or 1), hpp = 47 + (row or 1) * 6, dist = 6.6, marker = (row == 1) and 1 or nil },
    };
    local v = base[key];
    v.claim = v.claim or 'party';
    v.action = { name = 'Action Name', progress = 0.6 };
    v.tot = { name = 'Mob Target', claim = 'member' };
    v.debuffs = DEMO_DEBUFFS;
    v.resists = DEMO_RESISTS;
    return v;
end

-- ---------------------------------------------------------------------------
-- Drawing primitives
-- ---------------------------------------------------------------------------
local function texture(name)
    return TextureManager.getTexturePtr(TextureManager.getFileTexture('bar/' .. name));
end

-- enemybar2's bar: 1px left cap, full-width background, HP fill, 1px right cap at half
-- brightness, all tinted with the bar color. Falls back to plain rectangles if a texture
-- fails to load.
local function drawBar(dl, x, y, w, h, frac, color)
    local cap, bg, fg = texture('bg_cap'), texture('bg_body'), texture('fg_body');
    local col = util.toU32(color);
    frac = math.max(0, math.min(1, frac));
    if cap and bg and fg then
        dl:AddImage(cap, { x, y }, { x + 1, y + h }, { 0, 0 }, { 1, 1 }, col);
        dl:AddImage(bg, { x + 1, y }, { x + 1 + w, y + h }, { 0, 0 }, { 1, 1 }, col);
        if frac > 0 then
            dl:AddImage(fg, { x + 1, y }, { x + 1 + w * frac, y + h }, { 0, 0 }, { 1, 1 }, col);
        end
        dl:AddImage(cap, { x + 1 + w, y }, { x + 2 + w, y + h }, { 0, 0 }, { 1, 1 }, util.toU32(color, 0.5));
    else
        dl:AddRectFilled({ x, y }, { x + w + 2, y + h }, util.toU32({ color[1], color[2], color[3], 0.35 }));
        if frac > 0 then dl:AddRectFilled({ x + 1, y + 1 }, { x + 1 + w * frac, y + h - 1 }, col); end
    end
end

local function drawImage(dl, name, x, y, w, h, color)
    local tex = texture(name);
    if tex then dl:AddImage(tex, { x, y }, { x + w, y + h }, { 0, 0 }, { 1, 1 }, util.toU32(color)); end
end

local fontFamily = 'Arial';

local function measure(text, size)
    imtext.SetConfig(fontFamily, true, 1);
    return imtext.Measure(text, size);
end

-- align: 'left' (x is the left edge), 'right' (x is the right edge), 'center'
local function drawText(dl, text, x, y, color, size, align)
    imtext.SetConfig(fontFamily, true, 1);
    local w, h = imtext.Measure(text, size);
    if align == 'right' then x = x - w; elseif align == 'center' then x = x - w / 2; end
    imtext.Draw(dl, text, x, y, util.toARGB(color), size);
    return w, h;
end

-- ---------------------------------------------------------------------------
-- One bar row
-- ---------------------------------------------------------------------------
-- Height of the bar itself: the EKG monitor needs more room than a plain bar.
-- Regen-type effects shown on the mob (status ids from the buff table: Regen, Auto-Regen, Geo-Regen).
local REGEN_IDS = { [42] = true, [233] = true, [539] = true };
local function hasRegen(v)
    local ids = v.debuffs and v.debuffs.ids;
    if ids == nil then return false; end
    for _, id in pairs(ids) do
        if REGEN_IDS[id] then return true; end
    end
    return false;
end

local function barHeight(f)
    if f.style == 'ekg' then return f.ekgHeight or 34; end
    if f.style == 'farplane9' then return ff9.Height(f.fontSize); end
    if f.style == 'hearts' then
        local _, h = hearts.Size(f.heartCount or 10, f.heartsPerRow or 10, f.heartSize or 22);
        if f.showName or f.showHpp then
            local _, th = measure('Ag', f.fontSize);
            h = h + 2 + th;   -- name / HP% line under the hearts
        end
        return h;
    end
    return f.barHeight;
end

local function layout(f)
    local actionSize = math.max(8, math.floor(f.fontSize * 0.8));
    local distW = f.showDistance and (measure("999.9'", actionSize) + 6) or 0;
    local markerW = f.showTargetIcon and 16 or 0;
    local left = 4 + distW + markerW;
    local top = f.showAction and (actionSize + 6) or 4;
    if f.showResists then top = math.max(top, f.resistIconSize + 6); end
    local rightExtra = 4;
    if f.showInlineTot then rightExtra = rightExtra + 24 + measure('Target of Target Name', f.fontSize); end
    if f.showDebuffs and f.debuffPosition == 'right' then rightExtra = rightExtra + 6 + (f.iconSize + 1) * f.maxIcons; end
    local textOverflow = math.max(0, (measure('Ag', f.fontSize) - barHeight(f)) / 2);
    return {
        actionSize = actionSize, left = left, top = top + textOverflow,
        width = left + f.width + 2 + rightExtra,
        height = top + textOverflow * 2 + barHeight(f) + 2,
    };
end

local function drawRow(settings, f, v, originX, originY, lay)
    local dl = imgui.GetWindowDrawList();
    local x, y = originX + lay.left, originY + lay.top;
    local w, h = f.width, barHeight(f);
    local nameColor = settings.nameColors[v.claim] or WHITE;
    local isEkg = f.style == 'ekg';
    local isHearts = f.style == 'hearts';
    local isFF9 = f.style == 'farplane9';
    local heartsW, heartsH = 0, 0;

    if isFF9 then
        -- Farplane IX plate: name, HP% and gauge are drawn by eb/ff9.lua (no generic label).
        ff9.Draw(dl, tostring(f) .. ':' .. tostring(v.index or v.name), x, y, w + 2, h, v.hpp or 0,
            f.showName and v.name or '', nameColor, f.fontSize, drawText);
    elseif isHearts then
        -- Zelda heart containers; the name and HP go to the right of them.
        heartsW, heartsH = hearts.Draw(dl, tostring(f) .. ':' .. tostring(v.index or v.name), x, y, v.hpp or 0,
            f.heartCount or 10, f.heartsPerRow or 10, f.heartSize or 22, hasRegen(v));
    elseif isEkg then
        -- Resident Evil condition monitor; Fine / Caution / Danger in the bottom-right corner.
        ekg.Draw(dl, tostring(f) .. ':' .. tostring(v.index or v.name), x, y, w + 2, h, v.hpp or 0, function (word, color)
            local size = math.max(8, math.floor(f.fontSize * 0.95));
            local _, th = measure(word, size);
            drawText(dl, word, x + w - 4, y + h - th - 4, color, size, 'right');
        end);
    else
        drawBar(dl, x, y, w, h, (v.hpp or 0) / 100, f.colorByClaim and (f.claimColors[v.claim] or f.color) or f.color);
    end

    -- "Name: 79%" inside the bar (top-left of the monitor in EKG style).
    local label;
    if isFF9 then
        label = nil;
    elseif f.showName and f.showHpp then label = string.format('%s: %d%%', v.name, v.hpp);
    elseif f.showName then label = v.name;
    elseif f.showHpp then label = string.format('%d%%', v.hpp); end
    if label then
        local _, th = measure(label, f.fontSize);
        local ty = isEkg and (y + 3) or (y + (h - th) / 2);
        local tx = x + math.max(3, math.floor(w / 100)) + (isEkg and 2 or 0);
        if isHearts then
            tx, ty = x + 1, y + heartsH + 2;   -- under the hearts
        end
        drawText(dl, label, tx, ty, nameColor, f.fontSize, 'left');
    end

    -- Side elements center on this height: the hearts' first row in hearts style.
    local ch = h;
    if isHearts then
        local _, rowH = hearts.Size(1, 1, f.heartSize or 22);
        ch = rowH;
    end

    -- Left side: target marker, then distance.
    local leftEdge = x - 4;
    if f.showTargetIcon and v.marker then
        drawImage(dl, 'target', x - 16, y + (ch - 12) / 2, 12, 12, MARKER_COLORS[v.marker]);
    end
    if f.showTargetIcon then leftEdge = x - 20; end
    if f.showDistance and v.dist then
        local text = string.format("%.1f'", v.dist);
        local _, th = measure(text, lay.actionSize);
        drawText(dl, text, leftEdge, y + (ch - th) / 2, WHITE, lay.actionSize, 'right');
    end

    -- Above the left end: weaknesses / resistances and status immunities (from MobDB).
    if f.showResists and v.resists then
        local size = f.resistIconSize;
        local textSize = math.max(8, math.floor(size * 0.85));
        local rx, ry = x, y - size - 4;
        local function icon(name)
            local tex = TextureManager.getTexturePtr(TextureManager.getFileTexture(resists.IconPath(name)));
            if tex then dl:AddImage(tex, { rx, ry }, { rx + size, ry + size }, { 0, 0 }, { 1, 1 }, util.toU32(WHITE)); end
        end
        local function hover(x0, label)
            if imgui.IsMouseHoveringRect({ x0, ry }, { rx, ry + size }) then themedTooltip(label); end
        end
        for _, mod in ipairs(v.resists.mods) do
            local x0 = rx;
            local pct = math.floor((mod[2] - 1) * 100 + 0.5);
            local text = string.format('%+d%%', pct);
            icon(mod[1]);
            rx = rx + size + 1;
            local tw, th = drawText(dl, text, rx, ry + (size - textSize - 2) / 2, mod[2] > 1 and WEAK_COLOR or RESIST_COLOR, textSize, 'left');
            rx = rx + tw + 5;
            hover(x0, string.format('%s: takes %s damage', resists.LABELS[mod[1]] or mod[1], text));
        end
        if f.showImmunities and #v.resists.immune > 0 then
            rx = rx + (#v.resists.mods > 0 and 4 or 0);
            for _, name in ipairs(v.resists.immune) do
                local x0 = rx;
                icon(name);
                rx = rx + size + 1;
                hover(x0, 'Immune to ' .. (resists.LABELS[name] or name));
            end
        end
    end

    -- Above the right end: what it's casting or readying, with a progress line.
    if f.showAction and v.action then
        local right = x + w - math.max(3, math.floor(w / 100));
        local tw, th = drawText(dl, v.action.name, right, y - lay.actionSize - 4, nameColor, lay.actionSize, 'right');
        if f.showCastProgress and v.action.progress and not v.action.interrupted then
            dl:AddRectFilled({ right - tw, y - 3 }, { right - tw + tw * math.min(1, v.action.progress), y - 1 },
                util.toU32({ nameColor[1], nameColor[2], nameColor[3], 0.85 }));
        end
    end

    -- Right side: status icons (when placed there), then arrow + target-of-target name.
    local rightX = isHearts and (x + heartsW + 6) or (x + w + 4);
    if f.showDebuffs and f.debuffPosition == 'right' and v.debuffs then
        imgui.SetCursorScreenPos({ rightX + 2, y + (ch - f.iconSize) / 2 });
        statusIcons.DrawStatusIcons(v.debuffs.ids, f.iconSize, f.maxIcons, 1, false, nil,
            f.debuffTimers and v.debuffs.times or nil, nil, statusHandler, buffTable, v.debuffs.uncertain);
        local maxX = imgui.GetItemRectMax();
        rightX = math.max(rightX, maxX + 2);
    end
    if f.showInlineTot and v.tot then
        local totColor = settings.nameColors[v.tot.claim] or WHITE;
        drawImage(dl, 'attention', rightX + 4, y + (ch - 12) / 2, 12, 12, totColor);
        local _, th = measure(v.tot.name, f.fontSize);
        drawText(dl, v.tot.name, rightX + 20, y + (ch - th) / 2, totColor, f.fontSize, 'left');
    end

    -- Below: status icons in up to two rows.
    if f.showDebuffs and f.debuffPosition ~= 'right' and v.debuffs then
        imgui.SetCursorScreenPos({ x, y + h + 4 });
        statusIcons.DrawStatusIcons(v.debuffs.ids, f.iconSize, f.maxIcons, 2, false, nil,
            f.debuffTimers and v.debuffs.times or nil, nil, statusHandler, buffTable, v.debuffs.uncertain);
    end
end

-- ---------------------------------------------------------------------------
-- Frame windows
-- ---------------------------------------------------------------------------
local function snap(value) return math.floor(value / 10 + 0.5) * 10; end

-- Position handling: pinned to the saved spot normally; draggable in setup mode, where
-- the new spot is saved when the mouse is released (Ctrl snaps to a 10px grid).
local function frameWindow(key, f, x, y, rowsAbove, drawContents)
    if not M.setup or M.forcePos[key] then
        imgui.SetNextWindowPos({ x, y }, ImGuiCond_Always);
        M.forcePos[key] = nil;
    else
        imgui.SetNextWindowPos({ x, y }, ImGuiCond_Appearing);
    end
    local flags = BASE_FLAGS;
    if M.setup then
        imgui.SetNextWindowBgAlpha(0.25);
    else
        flags = bit.bor(flags, ImGuiWindowFlags_NoMove, ImGuiWindowFlags_NoBackground);
    end
    imgui.PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    imgui.PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, 3 });
    if imgui.Begin('enemybar_' .. key, { true }, flags) then
        drawContents();
        if M.setup then
            local wx, wy = imgui.GetWindowPos();
            -- The aggro stack anchors on its first bar, which sits at the bottom when stacking up.
            local anchorY = wy + (rowsAbove or 0);
            if math.abs(wx - f.x) > 0.5 or math.abs(anchorY - f.y) > 0.5 then
                M.pending[key] = { wx, anchorY };
            end
            local dl = imgui.GetWindowDrawList();
            local label = key == 'tot' and 'target of target' or key;
            drawText(dl, label, wx + 2, wy + 1, { 1, 1, 1, 0.6 }, 10, 'left');
        end
    end
    imgui.End();
    imgui.PopStyleVar(2);
end

local function renderSingle(settings, key, v)
    local f = settings.frames[key];
    if not f.show or v == nil then return; end
    local lay = layout(f);
    frameWindow(key, f, f.x, f.y, 0, function()
        local ox, oy = imgui.GetCursorScreenPos();
        imgui.Dummy({ lay.width, lay.height });
        drawRow(settings, f, v, ox, oy, lay);
    end);
end

local function renderAggro(settings, views)
    local f = settings.frames.aggro;
    if not f.show or #views == 0 then return; end
    local lay = layout(f);
    local pad = math.max(f.stackPadding, lay.height);
    local up = f.stackDir == 'up';
    local rows = #views;
    local rowsAbove = up and (rows - 1) * pad or 0;
    frameWindow('aggro', f, f.x, f.y - rowsAbove, rowsAbove, function()
        local ox, oy = imgui.GetCursorScreenPos();
        imgui.Dummy({ lay.width, (rows - 1) * pad + lay.height });
        for i, v in ipairs(views) do
            local slot = up and (rows - i) or (i - 1);
            drawRow(settings, f, v, ox, oy + slot * pad, lay);
        end
    end);
end

-- Main target and sub-target cursor indices. While the sub-target cursor is up, the game
-- moves the held target to slot 1 and the cursor to slot 0 (as XIUI handles it).
local function targetIndices()
    local t = AshitaCore:GetMemoryManager():GetTarget();
    local slot0, slot1 = t:GetTargetIndex(0) or 0, t:GetTargetIndex(1) or 0;
    if t:GetIsSubTargetActive() == 1 and slot1 ~= 0 then return slot1, slot0; end
    if slot0 == 0 and slot1 ~= 0 then return nil, slot1; end
    return (slot0 ~= 0) and slot0 or nil, nil;
end

local function inCutscene()
    local e = util.entity(util.myIndex());
    return e ~= nil and e.Status == 4;
end

function M.Render(settings)
    ekg.Prune();
    hearts.Prune();
    ff9.Prune();
    fontFamily = settings.fontFamily;
    if util.myServerId() == 0 then return; end
    if inCutscene() and not M.setup then return; end

    local frames = settings.frames;
    if M.setup then
        for _, key in ipairs({ 'target', 'tot', 'subtarget', 'focus' }) do renderSingle(settings, key, demoView(key)); end
        local demo = {};
        for i = 1, frames.aggro.count do demo[i] = demoView('aggro', i); end
        renderAggro(settings, demo);
    else
        local mainIdx, subIdx = targetIndices();
        if frames.target.show and mainIdx then renderSingle(settings, 'target', liveView(mainIdx, mainIdx, subIdx)); end
        if frames.tot.show and mainIdx then
            local totIndex = tracker.TargetOf(mainIdx);
            if totIndex then renderSingle(settings, 'tot', liveView(totIndex, mainIdx, subIdx)); end
        end
        if frames.subtarget.show and subIdx then renderSingle(settings, 'subtarget', liveView(subIdx, mainIdx, subIdx)); end
        local focusIdx = tracker.FocusIndex();
        if frames.focus.show and focusIdx then renderSingle(settings, 'focus', liveView(focusIdx, mainIdx, subIdx)); end
        if frames.aggro.show then
            local views = {};
            for _, row in ipairs(tracker.OrderedAggro(function(id)
                local d = debuffsFor(id); return d and d.ids or nil; end)) do
                if #views >= frames.aggro.count then break; end
                local v = liveView(row.index, mainIdx, subIdx,
                    { totIndex = row.pc and util.indexFromId(row.pc) or nil });
                if v then views[#views + 1] = v; end
            end
            renderAggro(settings, views);
        end
    end

    -- Save dragged positions once the mouse is released.
    if next(M.pending) ~= nil and not imgui.IsMouseDown(0) then
        local ctrl = false;
        pcall(function() ctrl = imgui.GetIO().KeyCtrl; end);
        for key, pos in pairs(M.pending) do
            local f = frames[key];
            f.x, f.y = pos[1], pos[2];
            if ctrl then f.x, f.y = snap(f.x), snap(f.y); M.forcePos[key] = true; end
        end
        M.pending = {};
        return true;  -- caller saves settings
    end
    return false;
end

function M.ResetPositions()
    for key in pairs(M.forcePos) do M.forcePos[key] = nil; end
    for _, key in ipairs({ 'target', 'tot', 'subtarget', 'focus', 'aggro' }) do M.forcePos[key] = true; end
end

return M;
