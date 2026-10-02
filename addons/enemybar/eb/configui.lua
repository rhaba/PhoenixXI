--[[
    enemybar - settings window (/eb). Returns true from Render when something changed so
    the caller can save.
]]

require('common');
local imgui = require('imgui');
local defaults = require('eb.defaults');
local render = require('eb.render');
local ui = require('phxui');

local M = { open = { false } };

-- ImGui widgets take one-element tables; these wrap plain settings fields.
local function checkbox(label, t, key)
    local ref = { t[key] == true };
    if imgui.Checkbox(label, ref) then t[key] = ref[1]; return true; end
    return false;
end

local function sliderInt(label, t, key, min, max)
    local ref = { t[key] };
    if imgui.SliderInt(label, ref, min, max) then t[key] = ref[1]; return true; end
    return false;
end

local function color(label, c)
    return imgui.ColorEdit4(label, c, ImGuiColorEditFlags_NoInputs) == true;
end

local function combo(label, current, options)
    local changed, value = false, current;
    if imgui.BeginCombo(label, current) then
        for _, option in ipairs(options) do
            if imgui.Selectable(option, option == current) then changed, value = true, option; end
        end
        imgui.EndCombo();
    end
    return changed, value;
end

local function claimPaletteEditor(id, palette)
    local changed = false;
    for _, key in ipairs(defaults.CLAIM_ORDER) do
        if color(defaults.CLAIM_LABELS[key] .. '##' .. id .. key, palette[key]) then changed = true; end
    end
    return changed;
end

local function frameTab(settings, key)
    local f = settings.frames[key];
    local changed = false;
    local id = '##' .. key;

    if checkbox('Show this bar' .. id, f, 'show') then changed = true; end
    imgui.SameLine();
    if ui.button('Reset position' .. id) then
        local d = defaults.settings.frames[key];
        f.x, f.y = d.x, d.y;
        render.forcePos[key] = true;
        changed = true;
    end
    ui.labelValue('Position', string.format('%d, %d', f.x, f.y), ui.color.peach);
    imgui.SameLine(); imgui.TextColored(ui.color.faint, '(drag it in setup mode)');

    ui.section('Style');
    local STYLE_LABELS = { bar = 'Bar', ekg = 'EKG monitor', hearts = 'Zelda hearts', farplane9 = 'Farplane IX' };
    local styleChanged, style = combo('Bar style' .. id, STYLE_LABELS[f.style] or 'Bar', { 'Farplane IX', 'Bar', 'EKG monitor', 'Zelda hearts' });
    if styleChanged then
        for key, label in pairs(STYLE_LABELS) do if label == style then f.style = key; end end
        changed = true;
    end
    if f.style == 'ekg' then
        imgui.TextColored(ui.color.dim, 'Resident Evil condition monitor: the trace turns from green to red and the heart rate rises as HP drops.');
    elseif f.style == 'hearts' then
        imgui.TextColored(ui.color.dim, 'Zelda heart containers: hearts empty a half at a time; the last heart throbs when HP is low.');
    elseif f.style == 'farplane9' then
        imgui.TextColored(ui.color.dim, 'Final Fantasy IX battle window in Farplane colors, matching the XivParty farplane9 layout.');
    end
    if ui.button('Use this style on every bar' .. id) then
        for _, k in ipairs(defaults.FRAME_ORDER) do settings.frames[k].style = f.style; end
        changed = true;
    end

    ui.section('Size');
    if sliderInt('Width' .. id, f, 'width', 60, 1200) then changed = true; end
    if f.style == 'ekg' then
        if sliderInt('Monitor height' .. id, f, 'ekgHeight', 18, 80) then changed = true; end
    elseif f.style == 'hearts' then
        if sliderInt('Hearts' .. id, f, 'heartCount', 1, 20) then changed = true; end
        if sliderInt('Hearts per row' .. id, f, 'heartsPerRow', 1, 20) then changed = true; end
        if sliderInt('Heart size' .. id, f, 'heartSize', 11, 44) then changed = true; end
    elseif f.style == 'farplane9' then
        -- plate height follows the font size
    elseif sliderInt('Bar height' .. id, f, 'barHeight', 4, 40) then changed = true; end
    if sliderInt('Font size' .. id, f, 'fontSize', 8, 32) then changed = true; end

    ui.section('Show');
    if checkbox('Name' .. id, f, 'showName') then changed = true; end
    imgui.SameLine(); if checkbox('HP %' .. id, f, 'showHpp') then changed = true; end
    imgui.SameLine(); if checkbox('Distance' .. id, f, 'showDistance') then changed = true; end
    if checkbox('Action being cast / readied' .. id, f, 'showAction') then changed = true; end
    imgui.SameLine(); if checkbox('Cast progress' .. id, f, 'showCastProgress') then changed = true; end
    if checkbox('Target / sub-target marker' .. id, f, 'showTargetIcon') then changed = true; end
    if checkbox('Target-of-target beside the bar' .. id, f, 'showInlineTot') then changed = true; end

    ui.section('Resistances (from MobDB)');
    if checkbox('Weaknesses / resistances above the bar' .. id, f, 'showResists') then changed = true; end
    imgui.SameLine(); if checkbox('Immunities' .. id, f, 'showImmunities') then changed = true; end
    if sliderInt('Resist icon size' .. id, f, 'resistIconSize', 8, 32) then changed = true; end
    if not require('eb.resists').Available() then
        imgui.TextColored(ui.color.warn, 'MobDB is not installed in addons/mobdb, so no resist data is available.');
    end

    ui.section('Buffs and debuffs');
    if checkbox('Show status icons' .. id, f, 'showDebuffs') then changed = true; end
    imgui.SameLine(); if checkbox('Timers' .. id, f, 'debuffTimers') then changed = true; end
    local posChanged, pos = combo('Placement' .. id, f.debuffPosition, { 'below', 'right' });
    if posChanged then f.debuffPosition = pos; changed = true; end
    if sliderInt('Icon size' .. id, f, 'iconSize', 10, 40) then changed = true; end
    if sliderInt('Max icons' .. id, f, 'maxIcons', 1, 32) then changed = true; end

    if key == 'aggro' then
        ui.section('Stack');
        if sliderInt('Bars' .. id, f, 'count', 1, 12) then changed = true; end
        if sliderInt('Spacing' .. id, f, 'stackPadding', 10, 80) then changed = true; end
        local dirChanged, dir = combo('Direction' .. id, f.stackDir, { 'down', 'up' });
        if dirChanged then f.stackDir = dir; changed = true; end
    end

    ui.section('Bar color');
    if color('HP fill' .. id, f.color) then changed = true; end
    if checkbox('Color by claim state' .. id, f, 'colorByClaim') then changed = true; end
    if f.colorByClaim then
        imgui.Indent();
        if claimPaletteEditor(key, f.claimColors) then changed = true; end
        if imgui.SmallButton('Default claim colors' .. id) then f.claimColors = defaults.claimPalette(); changed = true; end
        imgui.Unindent();
    end
    return changed;
end

local function generalTab(settings)
    local changed = false;
    ui.section('Theme');
    if ui.themeCombo('Window theme##eb_theme', settings.theme) then
        settings.theme = ui.theme;
        changed = true;
    end
    ui.tooltip('Colors for this settings window and the icon tooltips. Phoenix (ember red), Farplane\n(FFX ember and mist), Farplane9 (squared FF9 style, default), Umbrella (Resident Evil green), Midnight (blue) or Classic (stock ImGui).\nThe bars keep their own colors.');

    ui.section('Placement');
    local setup = { render.setup };
    if imgui.Checkbox('Setup mode: show demo bars and drag them (Ctrl snaps to a 10px grid)', setup) then
        render.setup = setup[1];
    end
    if ui.button('Reset all positions') then
        for _, key in ipairs(defaults.FRAME_ORDER) do
            local d = defaults.settings.frames[key];
            settings.frames[key].x, settings.frames[key].y = d.x, d.y;
        end
        render.ResetPositions();
        changed = true;
    end

    ui.section('Text and icons');
    local fontChanged, font = combo('Font', settings.fontFamily, defaults.FONTS);
    if fontChanged then settings.fontFamily = font; changed = true; end
    local themeChanged, theme = combo('Status icons', settings.statusIconTheme, { 'XIView', '-Default-' });
    if themeChanged then settings.statusIconTheme = theme; changed = true; end
    imgui.SameLine(); imgui.TextColored(ui.color.dim, '(-Default- = the game\'s own icons)');
    if checkbox('Mark inferred debuffs with "?"', settings, 'showUncertainMarker') then changed = true; end
    if sliderInt('Timer font size', settings, 'timerFontSize', 6, 20) then changed = true; end

    ui.section('Name and target-of-target text colors');
    if claimPaletteEditor('names', settings.nameColors) then changed = true; end
    return changed;
end

local function drawTabs(settings)
    local changed = false;
    if imgui.BeginTabBar('##enemybar_tabs') then
        if imgui.BeginTabItem('General') then
            if generalTab(settings) then changed = true; end
            imgui.EndTabItem();
        end
        for _, key in ipairs(defaults.FRAME_ORDER) do
            if imgui.BeginTabItem(defaults.FRAME_LABELS[key]) then
                if frameTab(settings, key) then changed = true; end
                imgui.EndTabItem();
            end
        end
        imgui.EndTabBar();
    end
    return changed;
end

function M.Render(settings)
    if not M.open[1] then return false; end
    -- The theme is pushed around Begin..End; the body runs under pcall so End and the pop
    -- always happen, and any error is re-raised afterwards for the caller to report.
    local token = ui.push();
    imgui.SetNextWindowSize({ 520, 640 }, ImGuiCond_FirstUseEver);
    local ok, result = true, false;
    if imgui.Begin('enemybar settings', M.open) then
        ok, result = pcall(drawTabs, settings);
    end
    imgui.End();
    ui.pop(token);
    if not ok then error(result, 0); end
    return result == true;
end

return M;
