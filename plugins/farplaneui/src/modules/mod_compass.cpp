/**
 * Farplane UI - compass and Vana'diel clock (far left, sitting on the chat panel's top edge).
 *
 * Compass: a flattened ring with N / E / S / W that turns with the camera (heading from the captured
 * world camera: forward = -(view _13, _23, _33); game X is east, game Z north), the player as the
 * centre dot.
 * Clock: Vana'diel time from the game's clock. Vana'diel runs 25x Earth time and Earth
 * 2002-01-01 00:00 JST (Unix 1009810800) is 0886/01/01 00:00, a Firesday; weeks are 8 days
 * Firesday .. Darksday.
 */
#include "common.hpp"

#include <cmath>
#include <cstring>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kDays[8] = {"Firesday", "Earthsday", "Watersday", "Windsday", "Iceday", "Lightningday", "Lightsday", "Darksday"};
        constexpr Rgba kDayColors[8] = {{0.92f, 0.30f, 0.22f, 1}, {0.86f, 0.70f, 0.32f, 1}, {0.35f, 0.58f, 0.95f, 1}, {0.40f, 0.82f, 0.45f, 1},
            {0.62f, 0.86f, 0.96f, 1}, {0.70f, 0.45f, 0.92f, 1}, {0.97f, 0.96f, 0.90f, 1}, {0.45f, 0.42f, 0.52f, 1}};

        struct VanaTime
        {
            int year, month, day, hour, minute, weekday;
        };
        auto Vana(uint32_t earth) -> VanaTime
        {
            const int64_t secs = (static_cast<int64_t>(earth) - 1009810800LL) * 25;
            const int64_t mins = secs / 60;
            const int64_t days = mins / (24 * 60);
            VanaTime v{};
            v.minute  = static_cast<int>(mins % 60);
            v.hour    = static_cast<int>((mins / 60) % 24);
            v.weekday = static_cast<int>(((days % 8) + 8) % 8);
            v.day     = static_cast<int>(days % 30) + 1;
            v.month   = static_cast<int>((days / 30) % 12) + 1;
            v.year    = 886 + static_cast<int>(days / 360);
            return v;
        }
    } // namespace

    void CompassModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        const float w = 150.0f * scale_, rx = 52.0f * scale_, ry = 26.0f * scale_;
        const float lineH = gui->GetTextLineHeight();
        const float clockH = showClock_ ? lineH + 4.0f : 0.0f;
        const float h = (showCompass_ ? ry * 2.0f + lineH + 8.0f : 0.0f) + clockH;
        // Far left, on the chat panel's top edge (it follows the chat as the band grows or shrinks).
        const float x = (ctx.layout.chat.valid() ? ctx.layout.chat.x : 8.0f) + offsetX_;
        const float y = (ctx.layout.chat.valid() ? ctx.layout.chat.y : ctx.layout.screenH * 0.7f) - h - 8.0f + offsetY_;
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_compass", nullptr, f))
        {
            auto shadowed = [&](const ImVec2& p, const Rgba& c, const char* text, float alpha = 1.0f) {
                gui->SetCursorScreenPos(ImVec2(p.x + 1, p.y + 1));
                gui->TextColored(ImVec4(0, 0, 0, 0.8f * alpha), "%s", text);
                gui->SetCursorScreenPos(p);
                gui->TextColored(ImVec4(c.r, c.g, c.b, c.a * alpha), "%s", text);
            };
            if (showCompass_)
            {
                const ImVec2 c(x + w * 0.5f, y + lineH * 0.5f + 4.0f + ry);
                // Camera heading: 0 facing north, increasing clockwise (toward east).
                float heading = 0.0f;
                if (ctx.camera.valid)
                {
                    const auto& v = ctx.camera.view;
                    heading = std::atan2(-v._13, -v._33);
                }
                // Ring (soft ellipse), then the four letters around it; up on screen = where the camera looks.
                if (IDirect3DTexture8* ring = ctx.textures->File(ctx.resourceDir + "ring.png"); ring != nullptr)
                    ui::ImageAt(gui, ring, ImVec2(c.x - rx, c.y - ry), ImVec2(rx * 2, ry * 2), ImVec2(0, 0), ImVec2(1, 1), Rgba{0.85f, 0.88f, 0.95f, 1}, 0.35f);
                ui::ImageAt(gui, ctx.white, ImVec2(c.x - rx, c.y - 0.5f), ImVec2(rx * 2, 1), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1}, 0.0f);
                const char* letters[4] = {"N", "E", "S", "W"};
                for (int i = 0; i < 4; ++i)
                {
                    const float a  = static_cast<float>(i) * 1.5707963f - heading; // angle from screen-up, clockwise
                    const float lx = c.x + std::sin(a) * rx, ly = c.y - std::cos(a) * ry;
                    const ImVec2 ts = gui->CalcTextSize(letters[i]);
                    // Letters behind the centre (lower half) are dimmer, like a ring lying on the ground.
                    const float depth = 0.55f + 0.45f * (0.5f - 0.5f * std::cos(a) * -1.0f);
                    shadowed(ImVec2(lx - ts.x * 0.5f, ly - ts.y * 0.5f), i == 0 ? Rgba{0.95f, 0.45f, 0.40f, 1} : Rgba{0.92f, 0.92f, 0.95f, 1}, letters[i], depth);
                }
                ui::ImageAt(gui, ctx.white, ImVec2(c.x - 2.5f, c.y - 2.5f), ImVec2(5, 5), ImVec2(0, 0), ImVec2(1, 1), art::orb_cyan);
            }
            if (showClock_)
            {
                const VanaTime v = Vana(mod::EarthTime());
                const float cy = y + h - clockH;
                ui::ImageAt(gui, ctx.white, ImVec2(x + 2, cy + lineH * 0.5f - 4.0f), ImVec2(8, 8), ImVec2(0, 0), ImVec2(1, 1), kDayColors[v.weekday]);
                // Like the game's clock: "7:30 (H-10)"; the dot's colour is the weekday's element.
                char b[64]{};
                ::sprintf_s(b, "%d:%02d", v.hour, v.minute);
                std::string line = b;
                if (!ctx.mapGrid.empty())
                    line += " (" + ctx.mapGrid + ")";
                if (showDay_)
                    line += std::string("  ") + kDays[v.weekday];
                ::strncpy_s(b, line.c_str(), _TRUNCATE);
                shadowed(ImVec2(x + 16, cy), Rgba{0.96f, 0.95f, 0.9f, 1}, b);
                if (showDate_)
                {
                    char d[32]{};
                    ::sprintf_s(d, "%d/%d/%d", v.year, v.month, v.day);
                    const float bw = gui->CalcTextSize(b).x;
                    shadowed(ImVec2(x + 16 + bw + 10, cy), Rgba{art::amber.r, art::amber.g, art::amber.b, 0.7f}, d);
                }
            }
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void CompassModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "compass";
        show_        = cfg->GetBool(alias, S, "show", show_);
        showCompass_ = cfg->GetBool(alias, S, "compass", showCompass_);
        showClock_   = cfg->GetBool(alias, S, "clock", showClock_);
        showDate_    = cfg->GetBool(alias, S, "date", showDate_);
        showDay_     = cfg->GetBool(alias, S, "day", showDay_);
        scale_       = std::clamp(cfg->GetFloat(alias, S, "scale", scale_), 0.6f, 2.0f);
        offsetX_     = cfg->GetFloat(alias, S, "offsetX", offsetX_);
        offsetY_     = cfg->GetFloat(alias, S, "offsetY", offsetY_);
    }
    void CompassModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "compass";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "compass", showCompass_);
        ui::SetB(cfg, alias, S, "clock", showClock_);
        ui::SetB(cfg, alias, S, "date", showDate_);
        ui::SetB(cfg, alias, S, "day", showDay_);
        ui::SetF(cfg, alias, S, "scale", scale_);
        ui::SetF(cfg, alias, S, "offsetX", offsetX_);
        ui::SetF(cfg, alias, S, "offsetY", offsetY_);
    }
    auto CompassModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
        {
            show_ = sub == "toggle" ? !show_ : sub == "show";
            ctx.MarkDirty();
            return true;
        }
        if (sub == "time")
        {
            const VanaTime v = Vana(mod::EarthTime());
            char b[96]{};
            ::sprintf_s(b, "Vana'diel %d/%d/%d %s %02d:%02d", v.year, v.month, v.day, kDays[v.weekday], v.hour, v.minute);
            ctx.Print(b);
            return true;
        }
        ctx.Print("/fpui compass [show|hide] | time");
        return true;
    }
    void CompassModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Compass and clock at the far left, on the chat panel.", show_);
        ch |= g.Toggle("Compass", "Ring with N/E/S/W that turns with the camera.", showCompass_);
        ch |= g.Toggle("Clock", "Vana'diel time and map grid square; the dot is the weekday's element colour.", showClock_);
        ch |= g.Toggle("Weekday name", "Write the weekday after the time.", showDay_);
        ch |= g.Toggle("Date", "Vana'diel date after the time.", showDate_);
        ch |= g.Slider("Scale", "Compass size.", scale_, 0.6f, 2.0f, "%.2f");
        ch |= g.Slider("Horizontal offset", "Move left (-) or right (+).", offsetX_, -400.0f, 1200.0f, "%.0f");
        ch |= g.Slider("Vertical offset", "Move up (-) or down (+).", offsetY_, -600.0f, 200.0f, "%.0f");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
