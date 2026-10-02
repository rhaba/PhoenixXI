--[[
Copyright (c) 2024 Thorny

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
--]]

local ui = require('phxui');
local imgui = require('imgui');
local panels = T { 'Buff', 'Debuff', 'Recast', 'Party', 'Custom' };
local sortTypes = T { 'Nominal', 'Percentage', 'Alphabetical', 'Creation' };
local trackModes = T { 'Self Cast Only', 'Party Only', 'Alliance Only', 'All Players' };

local blockeditor = require('blockeditor');
local config = {
    State = {
        IsOpen = { false },
    },
};

function config:LoadRenderers()
    local renderers = T{};
    local paths = T{
        string.format('%sconfig/addons/%s/resources/renderers/', AshitaCore:GetInstallPath(), addon.name),
        string.format('%saddons/%s/resources/renderers/', AshitaCore:GetInstallPath(), addon.name),
    };

    for _,path in ipairs(paths) do
        if not (ashita.fs.exists(path)) then
            ashita.fs.create_directory(path);
        end
        local contents = ashita.fs.get_directory(path, '.*\\.lua');
        for _,file in pairs(contents) do
            file = string.sub(file, 1, -5);
            if not renderers:contains(file) then
                renderers:append(file);
            end
        end
    end
    
    self.State.Renderers = renderers;
end

function config:LoadSkins()
    local skins = T{};
    local panels = T{ 'Buff', 'Debuff', 'Recast', 'Party', 'Custom' };
    for _,panelName in ipairs(panels) do
        local renderer = gSettings[panelName].Renderer;
        if (skins[renderer] == nil) then
            local newSkins = T{};
            local paths = T{
                string.format('%sconfig/addons/%s/resources/skins/%s/', AshitaCore:GetInstallPath(), addon.name, renderer),
                string.format('%saddons/%s/resources/skins/%s/', AshitaCore:GetInstallPath(), addon.name, renderer),
            };        
            for _,path in ipairs(paths) do
                if not (ashita.fs.exists(path)) then
                    ashita.fs.create_directory(path);
                end
                local contents = ashita.fs.get_directory(path, '.*\\.lua');
                for _,file in pairs(contents) do
                    file = string.sub(file, 1, -5);
                    if not newSkins:contains(file) then
                        newSkins:append(file);
                    end
                end
            end
            if (#newSkins > 0) then
                skins[renderer] = newSkins;
            end
        end
    end

    
    self.State.Skins = skins;
end

function config:DrawPanelTab(panelName)
    if imgui.BeginTabItem(string.format('%s##tTimersConfig%sTab', panelName, panelName)) then
        local panelSettings = gSettings[panelName];
        local panel = gPanels[panelName];
        local state = self.State;
        local skins = state.Skins[panelSettings.Renderer];

        if (skins ~= nil) then
            ui.section('Panel Skin');
            imgui.ShowHelp('Allows you to choose a skin to modify your current renderer.  Not all renderers are required to provide skins.');
            local activeSkin = panelSettings.Skin[panelSettings.Renderer];
            if (imgui.BeginCombo(string.format('##tTimersSkinSelection_%s', panelName), activeSkin, ImGuiComboFlags_None)) then
                for _,skin in ipairs(skins) do
                    if (imgui.Selectable(skin, skin == activeSkin)) then
                        if (skin ~= activeSkin) then
                            panelSettings.Skin[panelSettings.Renderer] = skin;
                            local skinPath = GetFilePath(string.format('skins/%s/%s.lua', panelSettings.Renderer, skin));
                            skinPath = GetFilePath(skinPath);
                            panel:UpdateSkin(LoadFile_s(skinPath));
                            settings.save();
                        end
                    end
                end
                imgui.EndCombo();
            end
        end
        ui.section('Draw Scale');
        imgui.ShowHelp('Allows you to resize the timer panel.');
        local buffer = { panelSettings.Scale };
        if (imgui.SliderFloat(string.format('##tTimersDrawScale_%s', panelName), buffer, 0.5, 3, '%.2f', ImGuiSliderFlags_AlwaysClamp)) then
            panelSettings.Scale = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        
        ui.section('Max Timers');
        imgui.ShowHelp('Determines the max number of timers to be shown at a time.');
        buffer = { panelSettings.MaxTimers };
        if (imgui.SliderInt(string.format('##tTimersMaxTimers_%s', panelName), buffer, 1, 20, '%u', ImGuiSliderFlags_AlwaysClamp)) then
            panelSettings.MaxTimers = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        
        ui.section('Completion Animation');
        imgui.ShowHelp('When enabled, timers will animate upon completion, for the specified duration in seconds, before disappearing.');
        buffer = { panelSettings.AnimateCompletion };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Animate Completion', panelName, 'AnimateCompletion'), buffer)) then
            panelSettings.AnimateCompletion = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        buffer = { panelSettings.CompletionDuration };
        if (imgui.SliderFloat(string.format('##tTimersCompletionDuration_%s', panelName), buffer, 0.5, 6, '%.2f', ImGuiSliderFlags_AlwaysClamp)) then
            panelSettings.CompletionDuration = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        
        ui.section('Sort Type');
        imgui.ShowHelp('Determines the order timers will be displayed in.');
        local activeSort = panelSettings.SortType;
        if (imgui.BeginCombo(string.format('##tTimersSortSelection_%s', panelName), activeSort, ImGuiComboFlags_None)) then
            for _,sortType in ipairs(sortTypes) do
                if (imgui.Selectable(sortType, sortType == activeSort)) then
                    if  (sortType ~= activeSort) then
                        panelSettings.SortType = sortType;
                        panel:UpdateSettings(panelSettings);
                        settings.save();
                    end
                end
            end
            imgui.EndCombo();
        end

        ui.section('General');
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Enabled', panelName, 'Enabled'), { panelSettings.Enabled })) then
            panelSettings.Enabled = not panelSettings.Enabled;
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('If not enabled, this panel won\'t show at all.');
        
        buffer = { panelSettings.ShiftCancel };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Allow Cancel', panelName, 'ShiftCancel'), buffer)) then
            panelSettings.ShiftCancel = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, shift-clicking a timer will remove it immediately.');
        
        buffer = { panelSettings.CtrlBlock };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Allow Block', panelName, 'CtrlBlock'), buffer)) then
            panelSettings.CtrlBlock = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, ctrl-clicking a timer will remove it and block that same timer from reappearing in the future.');
        
        buffer = { panelSettings.CountDown };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Count Down', panelName, 'CountDown'), buffer)) then
            panelSettings.CountDown = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, timers will begin full and count down to 0.');
        
        buffer = { panelSettings.ReverseColors };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Reverse Colors', panelName, 'ReverseColors'), buffer)) then
            panelSettings.ReverseColors = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, high and low colors are flipped.');
        
        
        buffer = { panelSettings.ShowTenths };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Show Tenths', panelName, 'ShowTenths'), buffer)) then
            panelSettings.ShowTenths = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, recast numbers will show 1/10 seconds when less than a minute remains.');
        
        buffer = { panelSettings.UseTooltips };
        if (imgui.Checkbox(string.format('%s##tTimersConfig_%s_%s', 'Show Tooltips', panelName, 'ShowTooltips'), buffer)) then
            panelSettings.UseTooltips = buffer[1];
            panel:UpdateSettings(panelSettings);
            settings.save();
        end
        imgui.ShowHelp('When enabled, hovering your mouse over a timer will show a tooltip if available.');
        if (panelName == 'Party') then
            ui.section('Party Members');
            local options = {
                { 'IncludeSelf', 'Include Yourself', 'Also show your own job ability recasts here (your Recast panel already has them).' },
                { 'IncludeAlliance', 'Include Alliance', 'Track members of the other two alliance parties too.' },
                { 'ShowMemberName', 'Show Member Name', 'Label timers "Name: Ability" instead of just the ability.' },
            };
            for _,option in ipairs(options) do
                buffer = { panelSettings[option[1]] == true };
                if (imgui.Checkbox(string.format('%s##tTimersConfig_Party_%s', option[2], option[1]), buffer)) then
                    panelSettings[option[1]] = buffer[1];
                    settings.save();
                end
                imgui.ShowHelp(option[3]);
            end
        end
        imgui.EndTabItem();
    end
end

function config:DrawContents()
    if imgui.BeginTabBar('##tTimersConfigTabBar', ImGuiTabBarFlags_NoCloseWithMiddleMouseButton) then
        self:DrawPanelTab('Buff');
        self:DrawPanelTab('Debuff');
        self:DrawPanelTab('Recast');
        self:DrawPanelTab('Party');
        self:DrawPanelTab('Custom');
        if imgui.BeginTabItem(string.format('Behavior##tTimersConfigBehaviorTab')) then
            ui.section('Buffs');
            if (imgui.Checkbox('Split By Duration##tTimersConfigBuffs_SplitByDuration', { gSettings.Buff.SplitByDuration })) then
                gSettings.Buff.SplitByDuration = not gSettings.Buff.SplitByDuration;
                settings.save();
            end
            imgui.ShowHelp('If enabled, the same buff will show up multiple times for each different duration.');
            local buffTrackMode = gSettings.Buff.TrackMode;
            if (imgui.BeginCombo(string.format('##tTimersBuffTrackMode'), buffTrackMode, ImGuiComboFlags_None)) then
                for _,trackMode in ipairs(trackModes) do
                    if (imgui.Selectable(trackMode, trackMode == buffTrackMode)) then
                        if (trackMode ~= buffTrackMode) then
                            gSettings.Buff.TrackMode = trackMode;
                            settings.save();
                        end
                    end
                end
                imgui.EndCombo();
            end
            imgui.ShowHelp('Determines which buffs will be recorded by the buff tracker.');
            ui.section('Debuffs');
            if (imgui.Checkbox('Split By Duration##tTimersConfigDebuffs_SplitByDuration', { gSettings.Debuff.SplitByDuration })) then
                gSettings.Debuff.SplitByDuration = not gSettings.Debuff.SplitByDuration;
                settings.save();
            end
            imgui.ShowHelp('If enabled, the same debuff will show up multiple times for each different duration.');
            local debuffTrackMode = gSettings.Debuff.TrackMode;
            if (imgui.BeginCombo(string.format('##tTimersDebuffTrackMode'), debuffTrackMode, ImGuiComboFlags_None)) then
                for _,trackMode in ipairs(trackModes) do
                    if (imgui.Selectable(trackMode, trackMode == debuffTrackMode)) then
                        if (trackMode ~= debuffTrackMode) then
                            gSettings.Debuff.TrackMode = trackMode;
                            settings.save();
                        end
                    end
                end
                imgui.EndCombo();
            end
            imgui.ShowHelp('Determines which debuffs will be recorded by the debuff tracker.');
            if (imgui.Checkbox('Show Mob Index##tTimersConfigDebuffs_ShowMobIndex', { gSettings.Debuff.ShowMobIndex })) then
                gSettings.Debuff.ShowMobIndex = not gSettings.Debuff.ShowMobIndex;
                settings.save();
            end
            imgui.ShowHelp('If enabled, debuffs will include mob index in the target name.  Changes will not apply to existing timers.');
            ui.section('Custom');
            if (imgui.Checkbox('Update Custom##tTimersConfigCustom_UpdateCustom', {gSettings.Custom.UpdateCustom})) then
                gSettings.Custom.UpdateCustom = not gSettings.Custom.UpdateCustom;
                settings.save();
            end
            imgui.ShowHelp("When enabled, custom timers will overwrite existing custom timers of the same name, rather than be created again.");
            ui.section('Display');
            if (imgui.Checkbox('Hide With Primitives##tTimersConfigCustom_HideWithPrimitives', {gSettings.HideWithPrimitives})) then
                gSettings.HideWithPrimitives = not gSettings.HideWithPrimitives;
                settings.save();
            end
            imgui.ShowHelp("When enabled, tTimers won't be drawn while Ashita's primitive manager is hidden.");
            ui.section('Theme');
            local pickedTheme = ui.themeCombo('Window theme##tt_theme', gSettings.WindowTheme);
            ui.tooltip('Colors this configuration window only; the timer panels use their skins.\nPhoenix: ember red. Farplane: FFX ember and mist. Farplane9: squared FF9 style (default). Umbrella: Resident Evil green.\nMidnight: blue. Classic: stock ImGui.');
            if (pickedTheme ~= nil) and (pickedTheme ~= gSettings.WindowTheme) then
                gSettings.WindowTheme = pickedTheme;
                settings.save();
            end
            imgui.EndTabItem();
        end
        blockeditor:DrawTabs();
        imgui.EndTabBar();
    end
end

function config:Render()
    local state = self.State;

    if (state.IsOpen[1]) then
        -- Theme the config window only; the timer panels keep their own renderer and skins.
        -- End and the style pops run even if a draw call errors, then the error is re-raised.
        local token = ui.push();
        local ok, err = pcall(function()
            local open = imgui.Begin(string.format('%s v%s Configuration', addon.name, addon.version), state.IsOpen, ImGuiWindowFlags_AlwaysAutoResize);
            local drawOk, drawErr = true, nil;
            if (open) then
                drawOk, drawErr = pcall(self.DrawContents, self);
            end
            imgui.End();
            if not drawOk then error(drawErr, 0); end
        end);
        ui.pop(token);
        if not ok then error(err, 0); end
    end
end

function config:Show()
    self.State.IsOpen[1] = true;
    self:LoadRenderers();
    self:LoadSkins();
end

return config;