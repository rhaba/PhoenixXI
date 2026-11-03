/**
 * Farplane UI - mask module: the bottom UI mask, its settings and commands.
 */

#include "../cfgui.hpp"
#include "../modules.hpp"

#include <algorithm>

namespace farplaneui
{
    void MaskModule::Direct3DInit(Context& ctx)
    {
        mask.Initialize(ctx.device, ctx.resourceDir); // missing PNGs get procedural stand-ins
        ctx.hfade = mask.TexHandle(Tex::HFade);
        ctx.white = mask.TexHandle(Tex::White);
    }

    void MaskModule::SetTheme(int index)
    {
        theme          = std::clamp(index, 0, kThemeCount - 1);
        settings.theme = kThemes[theme].name;
    }

    void MaskModule::Band(float& top, float& bottom) const
    {
        mask.ComputeBand(settings, top, bottom);
    }

    void MaskModule::Report(Context& ctx) const
    {
        ctx.Print(std::string("theme: ") + kThemes[theme].name + " (ini '" + settings.theme + "'), mask: " +
                  (settings.enabled ? "ENABLED" : "DISABLED (/fpui on)") + ", layer: " + settings.maskLayer);
        ctx.Print(std::string("sprite: ") + (mask.Ready() ? "OK" : "FAILED (D3DXCreateSprite) - mask cannot draw") + ", screen " +
                  std::to_string(mask.ScreenW()) + "x" + std::to_string(mask.ScreenH()));
        ctx.Print("asset dir: " + mask.assetDir_);
        std::string missing;
        for (size_t i = 0; i < static_cast<size_t>(mask.TextureCount()); ++i)
        {
            char buf[512]{};
            const std::string path = mask.assetDir_ + mask.TexName(i) + ".png";
            if (mask.TexFromFile(i))
                ::sprintf_s(buf, "  OK      %s", path.c_str());
            else
            {
                ::sprintf_s(buf, "  MISSING %s (hr 0x%08lX) -> %s", path.c_str(), static_cast<unsigned long>(mask.TexResult(i)),
                    mask.TexPresent(i) ? "built-in fallback" : "NO fallback");
                missing += (missing.empty() ? "" : ", ") + std::string(mask.TexName(i));
            }
            ctx.Print(buf);
        }
        if (!missing.empty())
            ctx.Print("!!! " + std::to_string(mask.TextureCount() - mask.TexturesLoaded()) + "/" + std::to_string(mask.TextureCount()) +
                      " mask PNGs NOT FOUND (" + missing + "). Copy the zip's assets\\*.png to " + mask.assetDir_ + " and reload.");
    }

    auto MaskModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        // Mask commands live at the top level (/fpui on, /fpui lines 10, ...) like the Lua addon.
        if (args.empty())
            return false;
        const std::string sub = ui::Lower(args[0]);
        const std::string arg = args.size() > 1 ? args[1] : std::string();
        auto& s = settings;
        int n   = 0;
        if (sub == "on" || sub == "off")
        {
            s.enabled = (sub == "on");
            ctx.Print(s.enabled ? "UI mask shown." : "UI mask hidden.");
        }
        else if (sub == "lines" && ui::ParseInt(arg, n))
        {
            s.lines = n;
            ctx.Print("UI mask covers " + std::to_string(s.lines) + " chat lines.");
        }
        else if (sub == "bottom" && ui::Lower(arg) == "auto")
        {
            s.bottomGapAuto = true;
            ctx.Print("Bottom gap: auto (3.6% of the screen height).");
        }
        else if (sub == "bottom" && ui::ParseInt(arg, n))
        {
            s.bottomGap     = n;
            s.bottomGapAuto = false;
            ctx.Print("Bottom gap: " + std::to_string(s.bottomGap) + "px.");
        }
        else if (sub == "top" && ui::ParseInt(arg, n))
        {
            if (arg[0] == '+' || arg[0] == '-')
                s.chatMargin -= n;
            else
                s.chatMargin = n;
            ctx.Print("Space above the chat: " + std::to_string(s.chatMargin) + "px.");
        }
        else if (sub == "layer")
        {
            const std::string what = ui::Lower(arg);
            if (what == "under" || what == "over")
                s.maskLayer = what + "_ui";
            else
                s.maskLayer = s.UnderUi() ? "over_ui" : "under_ui";
            ctx.Print(s.UnderUi() ? "UI mask under the game interface and addons." : "UI mask over the game interface (older mode).");
        }
        else if (sub == "setup")
        {
            mask.setup_ = !mask.setup_;
            ctx.Print(mask.setup_ ? "Setup outline on." : "Setup outline off.");
            return true;
        }
        else if (sub == "theme")
        {
            const int t = FindTheme(arg.c_str());
            if (t < 0)
            {
                std::string names;
                for (const auto& th : kThemes)
                    names += (names.empty() ? "" : ", ") + std::string(th.name);
                ctx.Print("Themes: " + names + ". Use /fpui theme <name>.");
                return true;
            }
            SetTheme(t);
            ctx.Print(std::string("Theme: ") + kThemes[t].name);
        }
        else
        {
            return false;
        }
        settings.Clamp();
        ctx.MarkDirty();
        return true;
    }

    void MaskModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!mask.setup_ || mask.top_ < 0.0f)
            return;
        const Palette& c = *ctx.palette;
        const float h    = static_cast<float>(mask.ScreenH());
        gui->SetNextWindowPos(ImVec2(12, mask.top_ - 24), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        if (gui->Begin("##farplaneui_setup", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            gui->TextColored(ToVec4(c.warn()), "farplaneui UI mask: %d lines x %dpx + %dpx, %.0fpx tall, %.0fpx gap below", settings.lines,
                settings.lineHeight, settings.padding, static_cast<double>(mask.bottom_ - mask.top_), static_cast<double>(h - mask.bottom_));
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void MaskModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        auto& s      = settings;
        bool changed = false;
        cfg::Grid g(gui, ctx);
        switch (ctx.settingsSection)
        {
        case 1: // General
        {
            changed |= g.Toggle("Enabled", "Show the UI mask band across the bottom of the screen.", s.enabled);
            g.Toggle("Setup outline", "Outline the band edges while adjusting the layout.", mask.setup_);
            const char* names[kThemeCount]{};
            for (int i = 0; i < kThemeCount; ++i)
                names[i] = kThemes[i].name;
            int t = theme;
            if (g.Choice("Theme", "Colour theme used by every FarplaneUI window.", t, names, kThemeCount))
            {
                SetTheme(t);
                changed = true;
            }
            bool under = s.UnderUi();
            if (g.Toggle("Under the game UI", "Draw the band beneath the game's own interface instead of over it.", under))
            {
                s.maskLayer = under ? "under_ui" : "over_ui";
                changed     = true;
            }
            changed |= g.Toggle("Hide in cutscenes", "Hide the band and panels during cutscenes (while the game hides its own interface). Talking to NPCs keeps the UI up.", s.hideInCutscenes);
            break;
        }
        case 2: // Size
        {
            changed |= g.Slider("Chat lines", "How many chat lines tall the band is.", s.lines, 1, 30);
            changed |= g.Slider("Line height", "Pixel height of one chat line.", s.lineHeight, 10, 40);
            changed |= g.Slider("Extra padding", "Extra space added to the band height.", s.padding, 0, 120);
            changed |= g.Slider("Top fade", "Height of the soft fade above the band.", s.fade, 0, 80);
            changed |= g.Slider("Space above chat", "Moves the band's top edge: lower values move it down, higher values up.", s.chatMargin, -300, 200);
            if (s.bottomGapAuto)
                s.bottomGap = mask.BottomGap(s, static_cast<float>(mask.ScreenH()));
            if (g.Slider("Bottom gap", "Space between the band and the bottom of the screen.", s.bottomGap, 0, 200))
            {
                s.bottomGapAuto = false;
                changed         = true;
            }
            changed |= g.Toggle("Auto bottom gap", "Fit the bottom gap to the game's own bottom bar.", s.bottomGapAuto);
            changed |= g.Slider("Opacity", "Band background opacity.", s.opacity, 0.2f, 1.0f, "%.2f");
            char h[32]{};
            ::sprintf_s(h, "%d px", mask.BandHeight(s));
            g.Info("Band height", h);
            break;
        }
        default: // Background (section 0)
        {
            // Band background: the classic fade or one of the artkit mask images.
            static const char* kBgNames[] = {"Classic fade", "Tall image (440)", "Tall image (500)", "Short image", "Image: low soft", "Image: medium strong", "Image: high soft"};
            static const char* kBgFiles[] = {"off", "auto", "tall500", "short", "mask_1920x1080_low_soft.png", "mask_1920x1080_med_strong.png", "mask_1920x1080_high_soft.png"};
            int bg = 1;
            for (int i = 0; i < 7; ++i)
                if (s.maskImage == kBgFiles[i])
                    bg = i;
            if (g.Choice("Background", "The band's background: the classic fade, or a Farplane mask image. Tall images rise past the strip panels; each picks the file for your screen width.", bg, kBgNames, 7))
            {
                s.maskImage = kBgFiles[bg];
                changed     = true;
            }
            changed |= g.Slider("Image opacity", "Opacity of the background image.", s.maskImageOpacity, 0.1f, 1.0f, "%.2f");
            changed |= g.Toggle("Accent line", "Ember accent line along the band's top edge.", s.accentLine);
            changed |= g.Toggle("Pyreflies", "Drifting pyreflies inside the band.", s.pyreflies);
            changed |= g.Slider("Pyrefly count", "How many pyreflies drift at once.", s.pyreflyCount, 0, 60);
            bool sending = s.Sending();
            if (g.Toggle("Sending trails", "Iridescent ribbon trails (the Sending) instead of simple motes.", sending))
            {
                s.pyreflyStyle = sending ? "sending" : "motes";
                mask.ResetFlies();
                changed = true;
            }
            break;
        }
        }
        if (changed)
        {
            s.Clamp();
            ctx.MarkDirty();
        }
    }
} // namespace farplaneui
