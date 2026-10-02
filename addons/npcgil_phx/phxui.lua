--[[
    phxui - shared look for Spongeh's PhoenixXI addons.

    Design follows Skold's PhoenixFishTrack (https://github.com/Skold177/PhoenixFishTrack): a deep
    ember-red panel, warm off-white text, crimson accents, rounded windows and frames, small
    upper-case section labels, and label-over-value stat cells. Each addon ships its own copy of
    this file so it stays standalone; keep the copies identical.

        local ui = require('phxui');
        local token = ui.push();          -- before imgui.Begin (alpha optional: ui.push(0.9))
        if imgui.Begin(...) then ... end
        imgui.End();
        ui.pop(token);

    Helpers (call inside a window): ui.section, ui.stat, ui.toggle, ui.rightText, ui.labelValue,
    ui.tooltip, ui.textWidth.

    Themes: ui.THEMES lists them (Phoenix, Farplane, Farplane9, Umbrella, Midnight, Classic). ui.setTheme(name)
    switches (call it once at load with the saved choice). ui.themeCombo(label, current) draws a
    picker; ui.themeMenu(current) adds a right-click menu to the current window for addons
    without a settings window.
]]

local imgui = require('imgui');

local ui = {};

local function rgb(hex, alpha)
    return {
        tonumber(hex:sub(1, 2), 16) / 255,
        tonumber(hex:sub(3, 4), 16) / 255,
        tonumber(hex:sub(5, 6), 16) / 255,
        alpha or 1.0,
    };
end

ui.rgb = rgb;

ui.color = {
    abyss     = rgb('180e0e'),        -- window background
    surface1  = rgb('291c1c'),        -- title bars, popups, alternate rows
    surface2  = rgb('321f1f'),        -- frames, headers, idle buttons
    border    = rgb('d2abab', 0.20),
    subtle    = rgb('d2abab', 0.12),
    text      = rgb('fff8f8'),
    secondary = rgb('eae1e1'),        -- names, list entries
    peach     = rgb('d2abab'),        -- highlighted values, the player
    muted     = rgb('8a6b6b'),        -- labels, hints
    faint     = rgb('5a4545'),        -- empty states, n/a
    royal     = rgb('c55151'),        -- accent: buttons, checks, sliders, bars
    tint      = rgb('c55151', 0.30),  -- hover on frames
    hover     = rgb('d45e5e'),
    ember     = rgb('ff8d79'),        -- attention, headline values
    danger    = rgb('e04040'),
    gold      = rgb('c5a131'),        -- rare / legendary / gil
    success   = rgb('63ba8a'),
    clear     = { 0, 0, 0, 0 },
};

-- Meaning-based names, so addons don't pick palette entries ad hoc.
ui.color.ok     = ui.color.success;
ui.color.warn   = ui.color.ember;
ui.color.bad    = ui.color.danger;
ui.color.info   = ui.color.peach;
ui.color.dim    = ui.color.muted;
ui.color.accent = ui.color.royal;

-- ---------------------------------------------------------------------------
-- Themes. Each is a full palette (hex, optional alpha). ui.setTheme copies the chosen one into
-- ui.color in place, so colors an addon captured at load time (local GOOD = ui.color.ok) follow.
-- 'Classic' keeps ImGui's stock window style and only supplies text colors.
-- ---------------------------------------------------------------------------
local PALETTES = {
    Phoenix = {
        abyss = '180e0e', surface1 = '291c1c', surface2 = '321f1f', border = { 'd2abab', 0.20 }, subtle = { 'd2abab', 0.12 },
        text = 'fff8f8', secondary = 'eae1e1', peach = 'd2abab', muted = '8a6b6b', faint = '5a4545',
        royal = 'c55151', tint = { 'c55151', 0.30 }, hover = 'd45e5e', ember = 'ff8d79',
        danger = 'e04040', gold = 'c5a131', success = '63ba8a',
    },
    Umbrella = {
        abyss = '0b120d', surface1 = '132018', surface2 = '1a2a20', border = { '9fd7a8', 0.20 }, subtle = { '9fd7a8', 0.12 },
        text = 'f2fff4', secondary = 'dcebdf', peach = 'a8d9b0', muted = '688a70', faint = '43584a',
        royal = '3f9e58', tint = { '3f9e58', 0.30 }, hover = '4fb86a', ember = 'f2c14e',
        danger = 'e04040', gold = 'd4b23c', success = '6fd98c',
    },
    Midnight = {
        abyss = '0e1218', surface1 = '182030', surface2 = '1f2a3b', border = { 'abc0d2', 0.20 }, subtle = { 'abc0d2', 0.12 },
        text = 'f6f9ff', secondary = 'dfe6ef', peach = 'a9c3de', muted = '6a7a90', faint = '475365',
        royal = '4f7fd1', tint = { '4f7fd1', 0.30 }, hover = '5f90e2', ember = 'ffa36b',
        danger = 'e05050', gold = 'd0ac3c', success = '5fc38f',
    },
    -- Final Fantasy X's Farplane: smoky charcoal sky, fields of burning amber and orange
    -- flowers, misty teal water, pale stone pillars and the cold blue orb at its heart.
    Farplane = {
        abyss = '141317', surface1 = '1f2024', surface2 = '2b2e34', border = { 'cfdcd8', 0.22 }, subtle = { 'cfdcd8', 0.12 },
        text = 'fff4e8', secondary = 'eadfd2', peach = 'a8d8f0', muted = '8d7a72', faint = '574a47',
        royal = 'd2642a', tint = { 'f08a3c', 0.28 }, hover = 'e8783a', ember = 'ffb347',
        danger = 'e5482f', gold = 'f2c45a', success = '7cc9b6',
    },
    -- Farplane IX: Farplane's colors in Final Fantasy IX's squared-off, framed menu style
    -- (square corners and thin borders come from SHAPES below).
    Farplane9 = {
        abyss = '141317', surface1 = '1f2024', surface2 = '2b2e34', border = { 'cfdcd8', 0.34 }, subtle = { 'cfdcd8', 0.16 },
        text = 'fff4e8', secondary = 'eadfd2', peach = 'a8d8f0', muted = '8d8580', faint = '57514f',
        royal = 'd2642a', tint = { 'f08a3c', 0.28 }, hover = 'e8783a', ember = 'ffb347',
        danger = 'ff5a3a', gold = 'ffd27a', success = '8fd6a0',
    },
    Classic = {
        abyss = '0f0f0f', surface1 = '1f1f1f', surface2 = '2a2a2a', border = { 'ffffff', 0.15 }, subtle = { 'ffffff', 0.08 },
        text = 'ffffff', secondary = 'e6e6e6', peach = 'b4c8e6', muted = '8c8c8c', faint = '5c5c5c',
        royal = '4296fa', tint = { '4296fa', 0.30 }, hover = '5aa5ff', ember = 'ffa54f',
        danger = 'ff5a5a', gold = 'e6c84f', success = '6ccf7f',
    },
};

ui.THEMES = { 'Phoenix', 'Farplane', 'Farplane9', 'Umbrella', 'Midnight', 'Classic' };

-- The pack's default theme. ui.adoptPackTheme switches an addon's saved theme to it once
-- (marking the settings table with phxThemeRev), so later choices made by the player stick.
ui.PACK_THEME = 'Farplane9';
ui.PACK_THEME_REV = 1;

function ui.adoptPackTheme(tbl, key)
    if type(tbl) ~= 'table' then return false; end
    if (tonumber(tbl.phxThemeRev) or 0) >= ui.PACK_THEME_REV then return false; end
    tbl[key] = ui.PACK_THEME;
    tbl.phxThemeRev = ui.PACK_THEME_REV;
    return true;
end
ui.theme = 'Phoenix';

function ui.setTheme(name)
    local p = PALETTES[name];
    if p == nil then name, p = 'Phoenix', PALETTES.Phoenix; end
    for key, value in pairs(p) do
        local hex, alpha = value, nil;
        if type(value) == 'table' then hex, alpha = value[1], value[2]; end
        local c = rgb(hex, alpha);
        local dst = ui.color[key];
        dst[1], dst[2], dst[3], dst[4] = c[1], c[2], c[3], c[4];
    end
    ui.theme = name;
    return name;
end

ui.PADDING = 12;

-- Some ImGui color names changed between versions (TabActive -> TabSelected); nil entries skip.
local function styleColors(alpha)
    local c = ui.color;
    return {
        { ImGuiCol_WindowBg,           { c.abyss[1], c.abyss[2], c.abyss[3], alpha } },
        { ImGuiCol_ChildBg,            c.clear },
        { ImGuiCol_TitleBg,            c.surface1 },
        { ImGuiCol_TitleBgActive,      c.surface2 },
        { ImGuiCol_TitleBgCollapsed,   c.surface1 },
        { ImGuiCol_MenuBarBg,          c.surface1 },
        { ImGuiCol_Border,             c.border },
        { ImGuiCol_Separator,          c.border },
        { ImGuiCol_SeparatorHovered,   c.hover },
        { ImGuiCol_SeparatorActive,    c.royal },
        { ImGuiCol_Text,               c.text },
        { ImGuiCol_TextDisabled,       c.muted },
        { ImGuiCol_FrameBg,            c.surface2 },
        { ImGuiCol_FrameBgHovered,     c.tint },
        { ImGuiCol_FrameBgActive,      c.surface2 },
        { ImGuiCol_SliderGrab,         c.royal },
        { ImGuiCol_SliderGrabActive,   c.hover },
        { ImGuiCol_PlotHistogram,      c.royal },
        { ImGuiCol_Header,             c.surface2 },
        { ImGuiCol_HeaderHovered,      c.tint },
        { ImGuiCol_HeaderActive,       c.royal },
        { ImGuiCol_Tab,                c.surface1 },
        { ImGuiCol_TabHovered,         c.hover },
        { ImGuiCol_TabSelected or ImGuiCol_TabActive, c.royal },
        { ImGuiCol_TabDimmed or ImGuiCol_TabUnfocused, c.surface1 },
        { ImGuiCol_TabDimmedSelected or ImGuiCol_TabUnfocusedActive, c.surface2 },
        { ImGuiCol_TableHeaderBg,      c.surface2 },
        { ImGuiCol_TableBorderLight,   c.subtle },
        { ImGuiCol_TableBorderStrong,  c.border },
        { ImGuiCol_TableRowBg,         c.clear },
        { ImGuiCol_TableRowBgAlt,      c.surface1 },
        { ImGuiCol_PopupBg,            c.surface1 },
        { ImGuiCol_CheckMark,          c.royal },
        { ImGuiCol_Button,             c.royal },
        { ImGuiCol_ButtonHovered,      c.hover },
        { ImGuiCol_ButtonActive,       c.royal },
        { ImGuiCol_ScrollbarBg,        c.abyss },
        { ImGuiCol_ScrollbarGrab,      c.surface2 },
        { ImGuiCol_ScrollbarGrabHovered, c.tint },
        { ImGuiCol_ScrollbarGrabActive, c.royal },
        { ImGuiCol_ResizeGrip,         c.clear },
        { ImGuiCol_ResizeGripHovered,  c.tint },
        { ImGuiCol_ResizeGripActive,   c.royal },
    };
end

-- Shape of each theme. Most are rounded; Farplane9 is squared off with thin frame borders,
-- like Final Fantasy IX's menus.
local SHAPES = {
    rounded = {
        { ImGuiStyleVar_WindowRounding, 8.0 },
        { ImGuiStyleVar_ChildRounding,  6.0 },
        { ImGuiStyleVar_FrameRounding,  4.0 },
        { ImGuiStyleVar_PopupRounding,  6.0 },
        { ImGuiStyleVar_GrabRounding,   4.0 },
        { ImGuiStyleVar_TabRounding,    4.0 },
        { ImGuiStyleVar_WindowPadding,  { 12, 10 } },
        { ImGuiStyleVar_ItemSpacing,    { 8, 5 } },
        { ImGuiStyleVar_CellPadding,    { 4, 3 } },
    },
    squared = {
        { ImGuiStyleVar_WindowRounding,   0.0 },
        { ImGuiStyleVar_ChildRounding,    0.0 },
        { ImGuiStyleVar_FrameRounding,    0.0 },
        { ImGuiStyleVar_PopupRounding,    0.0 },
        { ImGuiStyleVar_GrabRounding,     0.0 },
        { ImGuiStyleVar_TabRounding,      0.0 },
        { ImGuiStyleVar_ScrollbarRounding, 0.0 },
        { ImGuiStyleVar_WindowBorderSize, 1.0 },
        { ImGuiStyleVar_FrameBorderSize,  1.0 },
        { ImGuiStyleVar_PopupBorderSize,  1.0 },
        { ImGuiStyleVar_WindowPadding,    { 12, 10 } },
        { ImGuiStyleVar_ItemSpacing,      { 8, 5 } },
        { ImGuiStyleVar_CellPadding,      { 4, 3 } },
    },
};
local THEME_SHAPE = { Farplane9 = 'squared' };

-- True while the current theme is squared off (addons drawing their own plates can match it).
function ui.squared()
    return THEME_SHAPE[ui.theme] == 'squared';
end

-- Corner rounding to use for custom drawing in the current theme.
function ui.rounding(r)
    if ui.squared() then return 0; end
    return r;
end

-- Push the theme. Returns a token for ui.pop. alpha is the window background opacity.
function ui.push(alpha)
    local token = { colors = 0, vars = 0 };
    if ui.theme == 'Classic' then return token; end
    for _, entry in ipairs(styleColors(alpha or 0.94)) do
        if entry[1] ~= nil then
            imgui.PushStyleColor(entry[1], entry[2]);
            token.colors = token.colors + 1;
        end
    end
    for _, entry in ipairs(SHAPES[THEME_SHAPE[ui.theme] or 'rounded']) do
        if entry[1] ~= nil then
            imgui.PushStyleVar(entry[1], entry[2]);
            token.vars = token.vars + 1;
        end
    end
    return token;
end

function ui.pop(token)
    if token == nil then return; end
    if token.vars > 0 then imgui.PopStyleVar(token.vars); end
    if token.colors > 0 then imgui.PopStyleColor(token.colors); end
end

-- Push / pop around a draw function, with the pop guaranteed even if fn errors.
function ui.wrap(fn, alpha)
    local token = ui.push(alpha);
    local ok, err = pcall(fn);
    ui.pop(token);
    if not ok then error(err, 0); end
end

function ui.textWidth(text)
    local w = imgui.CalcTextSize(text);
    if type(w) == 'table' then return w.x or w[1] or 0; end
    return w or 0;
end

-- Small upper-case section label in the muted color, with a little space above.
function ui.section(label)
    imgui.Spacing();
    imgui.TextColored(ui.color.muted, tostring(label):upper());
end

-- Label-over-value cell, for use inside a table (moves to the next column first).
function ui.stat(label, value, color)
    imgui.TableNextColumn();
    imgui.TextColored(ui.color.muted, label);
    imgui.TextColored(color or ui.color.text, tostring(value));
end

-- "Label  value" on one line: muted label, colored value.
function ui.labelValue(label, value, color, indent)
    imgui.TextColored(ui.color.muted, label);
    if indent then imgui.SameLine(indent); else imgui.SameLine(); end
    imgui.TextColored(color or ui.color.text, tostring(value));
end

-- Text pinned to the right edge of the current content region, on the same line.
function ui.rightText(color, text)
    imgui.SameLine();
    local avail = imgui.GetContentRegionAvail();
    if type(avail) == 'table' then avail = avail.x or avail[1]; end
    local w = ui.textWidth(text);
    if avail and avail > w then imgui.SetCursorPosX(imgui.GetCursorPosX() + avail - w); end
    imgui.TextColored(color or ui.color.muted, text);
end

-- A button that reads as on (accent) or off (surface, muted text).
function ui.toggle(label, on, size)
    local c = ui.color;
    imgui.PushStyleColor(ImGuiCol_Button, on and c.royal or c.surface2);
    imgui.PushStyleColor(ImGuiCol_ButtonHovered, on and c.hover or c.tint);
    imgui.PushStyleColor(ImGuiCol_ButtonActive, c.royal);
    imgui.PushStyleColor(ImGuiCol_Text, on and c.text or c.muted);
    local clicked = imgui.Button(label, size or { 0, 0 });
    imgui.PopStyleColor(4);
    return clicked;
end

-- A quiet button (surface color) for secondary actions next to an accent one.
function ui.button(label, size)
    local c = ui.color;
    imgui.PushStyleColor(ImGuiCol_Button, c.surface2);
    imgui.PushStyleColor(ImGuiCol_ButtonHovered, c.tint);
    imgui.PushStyleColor(ImGuiCol_ButtonActive, c.royal);
    local clicked = imgui.Button(label, size or { 0, 0 });
    imgui.PopStyleColor(3);
    return clicked;
end

function ui.tooltip(text)
    if imgui.IsItemHovered() then imgui.SetTooltip(text); end
end

-- A combo listing the themes. Applies the pick and returns the new name, or nil if unchanged.
function ui.themeCombo(label, current)
    local picked = nil;
    if imgui.BeginCombo(label, current or ui.theme) then
        for _, name in ipairs(ui.THEMES) do
            if imgui.Selectable(name, name == (current or ui.theme)) then picked = name; end
        end
        imgui.EndCombo();
    end
    if picked then ui.setTheme(picked); end
    return picked;
end

-- Right-click anywhere in the current window for a Theme menu. Call between Begin and End.
-- Applies the pick and returns the new name, or nil.
function ui.themeMenu(current)
    local picked = nil;
    if imgui.BeginPopupContextWindow() then
        imgui.TextColored(ui.color.muted, 'THEME');
        for _, name in ipairs(ui.THEMES) do
            if imgui.MenuItem(name, nil, name == (current or ui.theme)) then picked = name; end
        end
        imgui.EndPopup();
    end
    if picked then ui.setTheme(picked); end
    return picked;
end

-- Parse a theme name typed in a command (any case). Returns the proper name or nil.
function ui.findTheme(text)
    text = tostring(text or ''):lower();
    for _, name in ipairs(ui.THEMES) do
        if name:lower() == text then return name; end
    end
    return nil;
end

return ui;
