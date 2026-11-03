/**
 * Farplane UI - settings persistence through Ashita's IConfigurationManager.
 *
 * File: <Ashita>/config/farplaneui/farplaneui.ini (IConfigurationManager paths are relative to
 * <Ashita>/config/), section [settings], same key names as the Lua addon.
 */

#include "settings.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace farplaneui
{
    namespace
    {
        constexpr auto kAlias   = "farplaneui";
        constexpr auto kFile    = "farplaneui/farplaneui.ini";
        constexpr auto kSection = "settings";

        // Make sure <Ashita>/config/farplaneui/farplaneui.ini exists so Load() succeeds on first run.
        void EnsureFile(IAshitaCore* core)
        {
            std::string dir = std::string(core->GetInstallPath()) + "\\config\\farplaneui";
            ::CreateDirectoryA(dir.c_str(), nullptr); // fine if it already exists
            const std::string path = dir + "\\farplaneui.ini";
            if (::GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                FILE* f = nullptr;
                if (::fopen_s(&f, path.c_str(), "w") == 0 && f != nullptr)
                {
                    ::fputs("[settings]\n", f);
                    ::fclose(f);
                }
            }
        }

        auto Str(IConfigurationManager* cfg, const char* key, const std::string& def) -> std::string
        {
            const char* v = cfg->GetString(kAlias, kSection, key);
            return (v != nullptr && v[0] != '\0') ? std::string(v) : def;
        }
    } // namespace

    void Settings::Clamp(void)
    {
        lines        = std::clamp(lines, 1, 30);
        lineHeight   = std::clamp(lineHeight, 10, 40);
        padding      = std::clamp(padding, 0, 120);
        fade         = std::clamp(fade, 0, 80);
        opacity      = std::clamp(opacity, 0.2f, 1.0f);
        pyreflyCount = std::clamp(pyreflyCount, 0, 60);
        chatMargin   = std::clamp(chatMargin, -300, 200);
        bottomGap    = std::clamp(bottomGap, 0, 200);
        if (pyreflyStyle != "motes")
            pyreflyStyle = "sending";
        if (maskLayer != "over_ui")
            maskLayer = "under_ui";
        maskImageOpacity = std::clamp(maskImageOpacity, 0.1f, 1.0f);
        if (maskImage.empty())
            maskImage = "auto";
    }

    void Settings::Load(IAshitaCore* core)
    {
        auto* cfg = core->GetConfigurationManager();
        if (cfg == nullptr)
            return;

        EnsureFile(core);
        if (!cfg->Load(kAlias, kFile))
            return; // keep defaults

        const Settings d{};
        enabled         = cfg->GetBool(kAlias, kSection, "enabled", d.enabled);
        theme           = Str(cfg, "theme", d.theme);
        lines           = cfg->GetInt32(kAlias, kSection, "lines", d.lines);
        lineHeight      = cfg->GetInt32(kAlias, kSection, "lineHeight", d.lineHeight);
        padding         = cfg->GetInt32(kAlias, kSection, "padding", d.padding);
        fade            = cfg->GetInt32(kAlias, kSection, "fade", d.fade);
        opacity         = cfg->GetFloat(kAlias, kSection, "opacity", d.opacity);
        accentLine      = cfg->GetBool(kAlias, kSection, "accentLine", d.accentLine);
        pyreflies       = cfg->GetBool(kAlias, kSection, "pyreflies", d.pyreflies);
        pyreflyStyle    = Str(cfg, "pyreflyStyle", d.pyreflyStyle);
        pyreflyCount    = cfg->GetInt32(kAlias, kSection, "pyreflyCount", d.pyreflyCount);
        hideInCutscenes = cfg->GetBool(kAlias, kSection, "hideInCutscenes", d.hideInCutscenes);
        followFancyChat = cfg->GetBool(kAlias, kSection, "followFancyChat", d.followFancyChat);
        chatMargin      = cfg->GetInt32(kAlias, kSection, "chatMargin", d.chatMargin);
        bottomGap       = cfg->GetInt32(kAlias, kSection, "bottomGap", d.bottomGap);
        bottomGapAuto   = cfg->GetBool(kAlias, kSection, "bottomGapAuto", d.bottomGapAuto);
        maskLayer       = Str(cfg, "maskLayer", d.maskLayer);
        maskImage       = Str(cfg, "maskImage", d.maskImage);
        maskImageOpacity = cfg->GetFloat(kAlias, kSection, "maskImageOpacity", d.maskImageOpacity);
        settingsRev     = cfg->GetInt32(kAlias, kSection, "settingsRev", d.settingsRev);
        showParty       = cfg->GetBool(kAlias, kSection, "showParty", d.showParty);
        Clamp();
    }

    void Settings::Save(IAshitaCore* core) const
    {
        auto* cfg = core->GetConfigurationManager();
        if (cfg == nullptr)
            return;

        char buf[32]{};
        auto setb = [&](const char* k, bool v) { cfg->SetValue(kAlias, kSection, k, v ? "1" : "0"); };
        auto seti = [&](const char* k, int v) {
            ::sprintf_s(buf, "%d", v);
            cfg->SetValue(kAlias, kSection, k, buf);
        };
        auto sets = [&](const char* k, const std::string& v) { cfg->SetValue(kAlias, kSection, k, v.c_str()); };

        setb("enabled", enabled);
        sets("theme", theme);
        seti("lines", lines);
        seti("lineHeight", lineHeight);
        seti("padding", padding);
        seti("fade", fade);
        ::sprintf_s(buf, "%.2f", static_cast<double>(opacity));
        cfg->SetValue(kAlias, kSection, "opacity", buf);
        setb("accentLine", accentLine);
        setb("pyreflies", pyreflies);
        sets("pyreflyStyle", pyreflyStyle);
        seti("pyreflyCount", pyreflyCount);
        setb("hideInCutscenes", hideInCutscenes);
        setb("followFancyChat", followFancyChat);
        seti("chatMargin", chatMargin);
        seti("bottomGap", bottomGap);
        setb("bottomGapAuto", bottomGapAuto);
        sets("maskLayer", maskLayer);
        sets("maskImage", maskImage);
        ::sprintf_s(buf, "%.2f", static_cast<double>(maskImageOpacity));
        cfg->SetValue(kAlias, kSection, "maskImageOpacity", buf);
        seti("settingsRev", settingsRev);
        setb("showParty", showParty);

        EnsureFile(core);
        cfg->Save(kAlias, kFile);
    }

} // namespace farplaneui
