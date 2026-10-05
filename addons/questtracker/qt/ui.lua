--[[
    questtracker - the on-screen tracker (WoW-style) and the quest journal.

    Tracker: a slim list of the quests you track, each with its current step. Finished steps
    disappear. Click a quest's name to fold it; right-click it for Step done / Undo / Untrack.
    Journal (/qt): every active quest with a Track checkbox, the full step list and the options.
]]

require('common');
local imgui = require('imgui');
local ui = require('phxui');
local E = require('qt.engine');

local M = {
    journalOpen = { false },
    selected = nil,
    search = { '' },
};

local function col(c, a) return { c[1], c[2], c[3], a or c[4] or 1 }; end

-- A small check or cross drawn in the text flow, then the requirement text.
local function requirement(text, ok)
    local C = ui.color;
    local x, y = imgui.GetCursorScreenPos();
    local h = imgui.GetTextLineHeight();
    local c = ok and C.ok or C.ember;
    local u = imgui.GetColorU32({ c[1], c[2], c[3], 1 });
    local dl = imgui.GetWindowDrawList();
    local s = h * 0.55;
    local x0, y0 = x + 2, y + h * 0.5;
    if ok then
        dl:AddLine({ x0, y0 }, { x0 + s * 0.38, y0 + s * 0.38 }, u, 2.0);
        dl:AddLine({ x0 + s * 0.38, y0 + s * 0.38 }, { x0 + s, y0 - s * 0.45 }, u, 2.0);
    else
        dl:AddLine({ x0, y0 - s * 0.45 }, { x0 + s * 0.85, y0 + s * 0.4 }, u, 2.0);
        dl:AddLine({ x0 + s * 0.85, y0 - s * 0.45 }, { x0, y0 + s * 0.4 }, u, 2.0);
    end
    imgui.Dummy({ s + 6, h });
    imgui.SameLine();
    imgui.TextColored(ok and C.secondary or C.ember, text);
end

local function requirements(id)
    for _, r in ipairs(E.Requirements(id)) do requirement(r[1], r[2]); end
end

local function where(s)
    local parts = {};
    if s.zone then parts[#parts + 1] = s.zone .. (s.grid and (' (' .. s.grid .. ')') or ''); end
    return table.concat(parts, ' ');
end

-- ---------------------------------------------------------------------------
-- Tracker overlay
-- ---------------------------------------------------------------------------
local function questPopup(id, q)
    if imgui.BeginPopupContextItem('##qtpop' .. id) then
        imgui.TextColored(ui.color.gold, q.name);
        imgui.Separator();
        if q.steps then
            if imgui.MenuItem('Step done') then E.setStep(id, E.step(id) + 1); end
            if imgui.MenuItem('Undo a step', nil, false, E.step(id) > 1) then E.setStep(id, E.step(id) - 1, true); end
        end
        if imgui.MenuItem('Untrack') then E.setTracked(id, false); end
        if imgui.MenuItem('Open journal') then M.journalOpen[1] = true; M.selected = id; end
        imgui.EndPopup();
    end
end

local function drawQuest(cfg, id, width, zoneNow)
    local q = E.quest(id);
    local C = ui.color;
    local folded = cfg.folded[tostring(id)] == true;
    local cur = E.step(id);
    local total = q.steps and #q.steps or 0;

    -- title line: name (click to fold) and step count
    imgui.PushStyleColor(ImGuiCol_Text, col(C.gold));
    if imgui.Selectable((folded and '+ ' or '') .. q.name .. '##qt' .. id, false, 0, { width - 40, 0 }) then
        cfg.folded[tostring(id)] = not folded or nil;
        cfg.dirty = true;
    end
    imgui.PopStyleColor();
    questPopup(id, q);
    if total > 0 then
        imgui.SameLine(width - 34);
        imgui.TextColored(C.muted, string.format('%d/%d', math.min(cur, total), total));
    end
    if folded then return; end

    imgui.Indent(10);
    if total == 0 then
        local s = q.start;
        imgui.TextColored(C.muted, s and ('Started by ' .. s.npc .. ', ' .. where(s) .. '. No step list for this quest yet.')
            or 'No step list for this quest yet.');
    elseif cur > total then
        imgui.TextColored(C.ok, 'All steps done. Finish it to complete the quest.');
    else
        local s = q.steps[cur];
        local here = s.zoneId and s.zoneId == zoneNow;
        if here then
            imgui.TextColored(C.ember, '>');
            imgui.SameLine();
        end
        imgui.TextColored(here and C.text or C.secondary, s.text);
        requirements(id);
        if cfg.showNext and q.steps[cur + 1] then
            imgui.TextColored(C.faint, 'Then: ' .. q.steps[cur + 1].text);
        end
    end
    imgui.Unindent(10);
    imgui.Spacing();
end

function M.DrawTracker(cfg)
    if not cfg.overlay then return; end
    local list = E.trackedList();
    if #list == 0 and not M.journalOpen[1] then return; end
    local width = cfg.width or 330;
    local flags = bit.bor(ImGuiWindowFlags_NoTitleBar, ImGuiWindowFlags_NoResize, ImGuiWindowFlags_AlwaysAutoResize,
        ImGuiWindowFlags_NoScrollbar, ImGuiWindowFlags_NoFocusOnAppearing, ImGuiWindowFlags_NoNav,
        ImGuiWindowFlags_NoSavedSettings);
    if cfg.locked then flags = bit.bor(flags, ImGuiWindowFlags_NoMove); end
    if (cfg.panelAlpha or 0) <= 0 then flags = bit.bor(flags, ImGuiWindowFlags_NoBackground); end
    if cfg.x and cfg.x >= 0 then
        imgui.SetNextWindowPos({ cfg.x, cfg.y }, ImGuiCond_FirstUseEver);
    else
        imgui.SetNextWindowPos({ imgui.GetIO().DisplaySize.x - width - 40, 220 }, ImGuiCond_FirstUseEver);
    end
    local alpha = cfg.panelAlpha or 0;
    local token = ui.push(alpha);
    if alpha <= 0 then imgui.PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0); end   -- no stray frame
    if imgui.Begin('questtracker##overlay', true, flags) then
        local C = ui.color;
        if alpha > 0 then
            -- the theme's accent line along the top of the panel
            local wx, wy = imgui.GetWindowPos();
            local a = C.accent;
            imgui.GetWindowDrawList():AddRectFilled({ wx + 1, wy + 1 }, { wx + imgui.GetWindowWidth() - 1, wy + 2 },
                imgui.GetColorU32({ a[1], a[2], a[3], math.min(1, alpha + 0.3) }));
        end
        imgui.PushTextWrapPos(imgui.GetCursorPosX() + width);
        -- header: click to minimize the tracker to this one line, click again to restore
        local header = (#list == 0 and 'QUESTS (none tracked)' or string.format('QUESTS (%d)', #list))
            .. (cfg.minimized and '   +' or '');
        imgui.PushStyleColor(ImGuiCol_Text, col(C.muted));
        if imgui.Selectable(header .. '##qthdr', false, 0, { width, 0 }) then
            cfg.minimized = not cfg.minimized or nil;
            cfg.dirty = true;
        end
        imgui.PopStyleColor();
        if imgui.IsItemHovered() then imgui.SetTooltip(cfg.minimized and 'Show the tracked quests' or 'Minimize'); end
        if not cfg.minimized then imgui.Spacing(); end
        local zoneNow = E.currentZone();
        for i, id in ipairs(cfg.minimized and {} or list) do
            if i > (cfg.maxQuests or 8) then
                imgui.TextColored(C.faint, string.format('+%d more in the journal (/qt)', #list - i + 1));
                break;
            end
            drawQuest(cfg, id, width, zoneNow);
        end
        imgui.PopTextWrapPos();
        -- right-click the tracker's background: opacity and the other tracker options
        if imgui.BeginPopupContextWindow('##qtbg', bit.bor(ImGuiPopupFlags_MouseButtonRight, ImGuiPopupFlags_NoOpenOverItems)) then
            imgui.TextColored(C.muted, 'Quest tracker');
            imgui.Separator();
            imgui.PushItemWidth(160);
            local p = { cfg.panelAlpha or 0 };
            if imgui.SliderFloat('Background##qtbga', p, 0, 1, '%.2f') then cfg.panelAlpha = p[1]; cfg.dirty = true; end
            imgui.PopItemWidth();
            if imgui.MenuItem(cfg.minimized and 'Restore' or 'Minimize') then cfg.minimized = not cfg.minimized or nil; cfg.dirty = true; end
            if imgui.MenuItem('Lock position', nil, cfg.locked == true) then cfg.locked = not cfg.locked; cfg.dirty = true; end
            if imgui.MenuItem('Open journal') then M.journalOpen[1] = true; end
            if imgui.MenuItem('Hide tracker') then cfg.overlay = false; cfg.dirty = true; end
            imgui.EndPopup();
        end
        local x, y = imgui.GetWindowPos();
        if not cfg.locked and (x ~= cfg.x or y ~= cfg.y) then cfg.x, cfg.y = x, y; cfg.dirty = true; end
    end
    imgui.End();
    if alpha <= 0 then imgui.PopStyleVar(); end
    ui.pop(token);
end

-- ---------------------------------------------------------------------------
-- Journal
-- ---------------------------------------------------------------------------
-- Text cut to fit a width in pixels, with an ellipsis.
local function fit(text, width)
    if imgui.CalcTextSize(text) <= width then return text; end
    local s = text;
    while #s > 1 and imgui.CalcTextSize(s .. '...') > width do s = s:sub(1, -2); end
    return s .. '...';
end

-- A line of wrapped text that starts level with a checkbox / button on its left.
local function framedText(color, text)
    imgui.AlignTextToFramePadding();
    imgui.TextColored(color, text);
end

local function drawDetails(cfg, id)
    local q = E.quest(id);
    local C = ui.color;
    imgui.PushTextWrapPos(0);
    imgui.TextColored(C.gold, q.name);
    imgui.TextColored(C.muted, (E.LOG_LABELS[q.log] or '') .. (q.repeatable and '  -  repeatable' or ''));
    if q.start then
        imgui.TextColored(C.muted, 'Started by ' .. q.start.npc .. ', ' .. where(q.start));
    end
    if q.note then
        imgui.Spacing();
        imgui.TextColored(C.peach, q.note);
    end
    ui.section('Steps');
    if not q.steps then
        imgui.TextColored(C.faint, 'No step list for this quest yet (it is coded in NPC scripts the generator doesn\'t read).');
    else
        local cur = E.step(id);
        if E.needsReview(id) then
            framedText(C.ember, 'Already partway through? Tick the steps you\'ve done.');
            imgui.SameLine();
            if imgui.SmallButton('None yet##qtnone') then E.reviewed(id); end
            imgui.Spacing();
        end
        for i, s in ipairs(q.steps) do
            local done, now = i < cur, i == cur;
            -- ticking a step marks it and everything before it done; unticking goes back to it
            local box = { done };
            if imgui.Checkbox('##qtstep' .. id .. '_' .. i, box) then
                E.setStep(id, box[1] and (i + 1) or i, true);
            end
            if imgui.IsItemHovered() then
                imgui.SetTooltip(done and 'Untick to go back to this step' or 'Tick: done up to here');
            end
            imgui.SameLine();
            framedText(done and C.faint or (now and C.text or C.secondary),
                string.format('%s%d. %s', now and '> ' or '', i, s.text));
            if now then
                imgui.Indent(28);
                requirements(id);
                if s.kills then
                    -- adjust by hand (kills made before the addon was counting, or a missed one)
                    for _, d in ipairs({ -10, -1, 1, 10 }) do
                        if imgui.SmallButton(string.format('%+d##qtk%d_%d', d, id, d)) then E.setKills(id, E.kills(id) + d); end
                        imgui.SameLine();
                    end
                    imgui.TextColored(C.faint, 'adjust the count');
                end
                imgui.Unindent(28);
            end
        end
    end
    if q.rewards then
        ui.section('Rewards');
        local r, parts = q.rewards, {};
        if r.gil then parts[#parts + 1] = tostring(r.gil) .. ' gil'; end
        for _, n in ipairs(r.items or {}) do parts[#parts + 1] = n; end
        for _, n in ipairs(r.keyItems or {}) do parts[#parts + 1] = n .. ' (key item)'; end
        imgui.TextColored(C.secondary, table.concat(parts, ', '));
    end
    imgui.PopTextWrapPos();
end

local LIST_W = 330;

local function drawList()
    local C = ui.color;
    local list = E.activeList();
    -- add a quest you're on (works before the server's quest log has arrived)
    imgui.PushItemWidth(-1);
    imgui.InputTextWithHint('##qtsearch', "Add a quest you're on...", M.search, 64);
    imgui.PopItemWidth();
    local hits = E.search(M.search[1], 8);
    if #hits > 0 then
        for _, hid in ipairs(hits) do
            if ui.button('+##add' .. hid) then
                E.addManual(hid);
                M.selected = hid;
                M.search[1] = '';
            end
            imgui.SameLine();
            local hq = E.quest(hid);
            framedText(C.secondary, fit(hq.name, LIST_W - 130));
            imgui.SameLine(LIST_W - 100);
            framedText(C.faint, E.LOG_LABELS[hq.log] or '');
        end
        imgui.Separator();
    elseif #M.search[1] >= 2 then
        imgui.TextColored(C.faint, 'No matching quest (or you already have it).');
    end
    if #list == 0 then
        imgui.PushTextWrapPos(0);
        imgui.TextColored(C.muted, 'No active quests known yet. Zone once so the server sends your quest log, or add quests with the box above.');
        imgui.PopTextWrapPos();
    end
    local lastLog = nil;
    local rowW = LIST_W - 28;
    for _, id in ipairs(list) do
        local q = E.quest(id);
        if q.log ~= lastLog then
            lastLog = q.log;
            ui.section(E.LOG_LABELS[q.log] or ('Log ' .. q.log));
        end
        local t = { E.isTracked(id) };
        if imgui.Checkbox('##trk' .. id, t) then E.setTracked(id, t[1]); end
        if imgui.IsItemHovered() then imgui.SetTooltip('Show on the tracker'); end
        imgui.SameLine();
        -- name (trimmed to fit), with the step count right-aligned and a ! when progress needs checking
        local count = q.steps and string.format('%d/%d', math.min(E.step(id), #q.steps), #q.steps) or '';
        local review = q.steps and E.needsReview(id);
        local countW = imgui.CalcTextSize(count) + (review and 14 or 0);
        local x0 = imgui.GetCursorPosX();
        imgui.AlignTextToFramePadding();
        if imgui.Selectable(fit(q.name, rowW - x0 - countW - 16) .. '##sel' .. id, M.selected == id, 0, { rowW - x0, 0 }) then
            M.selected = id;
        end
        if review and imgui.IsItemHovered() then imgui.SetTooltip('Check your progress: tick the steps you\'ve done'); end
        imgui.SameLine(rowW - countW);
        if review then framedText(C.ember, '!'); imgui.SameLine(); end
        framedText(C.muted, count);
    end
end

local function drawOptions(cfg, onTheme)
    local C = ui.color;
    local function check(label, key)
        local b = { cfg[key] == true };
        if imgui.Checkbox(label, b) then cfg[key] = b[1]; cfg.dirty = true; end
    end
    ui.section('Tracking');
    check('Track new quests automatically', 'autoTrack');
    ui.section('On-screen tracker');
    check('Show the tracker', 'overlay');
    check('Also show the step after the current one', 'showNext');
    check('Lock its position', 'locked');
    imgui.PushItemWidth(200);
    local w = { cfg.width or 330 };
    if imgui.SliderInt('Width', w, 220, 600) then cfg.width = w[1]; cfg.dirty = true; end
    local p = { cfg.panelAlpha or 0 };
    if imgui.SliderFloat('Background', p, 0, 1, '%.2f') then cfg.panelAlpha = p[1]; cfg.dirty = true; end
    ui.tooltip('How solid the box behind the tracker is (0 = no box). Also: right-click the tracker.');
    local m = { cfg.maxQuests or 8 };
    if imgui.SliderInt('Quests shown', m, 1, 15) then cfg.maxQuests = m[1]; cfg.dirty = true; end
    ui.section('Look');
    if ui.themeCombo('Theme##qttheme', cfg.theme) then onTheme(ui.theme); end
    imgui.PopItemWidth();
    imgui.Spacing();
    imgui.TextColored(C.faint, 'Right-click any quest journal window for the theme menu.');
end

function M.DrawJournal(cfg, onTheme)
    if not M.journalOpen[1] then return; end
    imgui.SetNextWindowSize({ 820, 500 }, ImGuiCond_FirstUseEver);
    local token = ui.push();
    if imgui.Begin('Quest Journal##questtracker', M.journalOpen, ImGuiWindowFlags_None) then
        local C = ui.color;
        if imgui.BeginTabBar('##qttabs') then
            if imgui.BeginTabItem('Quests') then
                imgui.BeginChild('##qtlist', { LIST_W, 0 }, ImGuiChildFlags_Borders);
                drawList();
                imgui.EndChild();
                imgui.SameLine();
                imgui.BeginChild('##qtdetail', { 0, 0 }, ImGuiChildFlags_Borders);
                if M.selected and E.quest(M.selected) then
                    drawDetails(cfg, M.selected);
                else
                    imgui.TextColored(C.muted, 'Pick a quest on the left.');
                end
                imgui.EndChild();
                imgui.EndTabItem();
            end
            if imgui.BeginTabItem('Options') then
                drawOptions(cfg, onTheme);
                imgui.EndTabItem();
            end
            imgui.EndTabBar();
        end
        if ui.themeMenu(cfg.theme) then onTheme(ui.theme); end
    end
    imgui.End();
    ui.pop(token);
end

return M;
