/**
 * Farplane UI - player status effects (top-left, where the game draws its own).
 *
 * The player's status icons in game order, each with its time left underneath in the native style
 * ("5h", "15m", "42"). Read from memory through mod::PlayerBuffs; hovering an icon shows its name.
 */
#include "common.hpp"

namespace farplaneui
{
    namespace
    {
        // Native-style short time: whole hours, then whole minutes, then seconds.
        auto Short(double s) -> std::string
        {
            char b[16]{};
            const int t = static_cast<int>(std::ceil(s));
            if (t >= 3600)
                ::sprintf_s(b, "%dh", t / 3600);
            else if (t >= 60)
                ::sprintf_s(b, "%dm", t / 60);
            else
                ::sprintf_s(b, "%d", t);
            return b;
        }
        auto Long(double s) -> std::string
        {
            char b[24]{};
            const int t = static_cast<int>(std::ceil(s));
            if (t >= 3600)
                ::sprintf_s(b, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
            else
                ::sprintf_s(b, "%d:%02d", t / 60, t % 60);
            return b;
        }
    } // namespace

    void StatusModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        const auto buffs = mod::PlayerBuffs(ctx);
        if (buffs.empty())
            return;
        const float size = iconSize_, gap = 3.0f, textSize = std::max(10.0f, size * 0.46f);
        const float cellW = size + gap, cellH = size + textSize + 3.0f;
        const int perRow  = std::max(1, perRow_);
        const int count   = static_cast<int>(buffs.size());
        const int rows    = (count + perRow - 1) / perRow;
        gui->SetNextWindowPos(ImVec2(x_, y_), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(cellW * static_cast<float>(std::min(count, perRow)) + 4.0f, cellH * static_cast<float>(rows) + 4.0f), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | (locked_ ? ImGuiWindowFlags_NoInputs : 0) | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
        if (gui->Begin("##fpui_status", nullptr, f))
        {
            // Unlocked: the whole grid is a drag handle (outlined so it's easy to find).
            if (!locked_)
            {
                const ImVec2 area(cellW * static_cast<float>(std::min(count, perRow)), cellH * static_cast<float>(rows));
                gui->SetCursorScreenPos(ImVec2(x_, y_));
                gui->InvisibleButton("##drag", area);
                if (gui->IsItemActive())
                {
                    const ImVec2 d = gui->GetIO().MouseDelta;
                    x_ = std::max(0.0f, x_ + d.x), y_ = std::max(0.0f, y_ + d.y);
                    ctx.MarkDirty();
                }
                if (ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, ImVec2(x_ - 2, y_ - 2), ImVec2(area.x + 4, area.y + 4), ImVec2(0, 0), ImVec2(1, 1), art::gold_hi, 0.18f);
            }
            const ImVec2 mouse = gui->GetIO().MousePos;
            gui->PushFont(nullptr, textSize);
            for (int i = 0; i < count; ++i)
            {
                const auto& b = buffs[static_cast<size_t>(i)];
                const float x = x_ + static_cast<float>(i % perRow) * cellW;
                const float y = y_ + static_cast<float>(i / perRow) * cellH;
                if (IDirect3DTexture8* tex = mod::StatusIcon(ctx, b.id); tex != nullptr)
                    ui::ImageAt(gui, tex, ImVec2(x, y), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                if (b.seconds > 0)
                {
                    const std::string t = Short(b.seconds);
                    const float tw = gui->CalcTextSize(t.c_str()).x;
                    // Under a minute: amber, so expiring effects stand out.
                    const ImVec4 col = b.seconds < 60 ? ToVec4(art::gold_hi) : ImVec4(0.96f, 0.95f, 0.9f, 1.0f);
                    const ImVec2 at(x + (size - tw) * 0.5f, y + size);
                    gui->SetCursorScreenPos(ImVec2(at.x + 1, at.y + 1));
                    gui->TextColored(ImVec4(0, 0, 0, 0.85f), "%s", t.c_str());
                    gui->SetCursorScreenPos(at);
                    gui->TextColored(col, "%s", t.c_str());
                }
                if (mouse.x >= x && mouse.y >= y && mouse.x < x + size && mouse.y < y + size)
                {
                    if (b.seconds > 0)
                        gui->SetTooltip("%s  (%s)", mod::BuffName(ctx, b.id).c_str(), Long(b.seconds).c_str());
                    else
                        gui->SetTooltip("%s", mod::BuffName(ctx, b.id).c_str());
                }
            }
            gui->PopFont();
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void StatusModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "status";
        show_     = cfg->GetBool(alias, S, "show", show_);
        iconSize_ = std::clamp(cfg->GetFloat(alias, S, "iconSize2", iconSize_), 16.0f, 48.0f);
        perRow_   = std::clamp(cfg->GetInt32(alias, S, "perRow", perRow_), 4, 32);
        x_        = cfg->GetFloat(alias, S, "x", x_);
        y_        = cfg->GetFloat(alias, S, "y", y_);
        locked_   = cfg->GetBool(alias, S, "locked", locked_);
    }
    void StatusModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "status";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetF(cfg, alias, S, "iconSize2", iconSize_);
        ui::SetI(cfg, alias, S, "perRow", perRow_);
        ui::SetF(cfg, alias, S, "x", x_);
        ui::SetF(cfg, alias, S, "y", y_);
        ui::SetB(cfg, alias, S, "locked", locked_);
    }
    auto StatusModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
        {
            show_ = sub == "toggle" ? !show_ : sub == "show";
            ctx.MarkDirty();
            return true;
        }
        if ((sub == "pos" || sub == "move") && args.size() >= 3)
        {
            x_ = std::max(0.0f, static_cast<float>(std::atof(args[1].c_str())));
            y_ = std::max(0.0f, static_cast<float>(std::atof(args[2].c_str())));
            ctx.MarkDirty();
            return true;
        }
        if (sub == "lock" || sub == "unlock")
        {
            locked_ = sub == "lock";
            ctx.MarkDirty();
            return true;
        }
        if (sub == "reset")
        {
            x_ = 6.0f, y_ = 6.0f;
            ctx.MarkDirty();
            return true;
        }
        ctx.Print("/fpui status [show|hide] | pos <x> <y> | unlock (drag to move) | lock | reset");
        return true;
    }
    void StatusModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Your status effects in the top-left corner, with time left under each icon.", show_);
        ch |= g.Slider("Icon size", "Icon size in pixels.", iconSize_, 16.0f, 48.0f, "%.0f");
        ch |= g.Slider("Icons per row", "Wrap to a new row after this many.", perRow_, 4, 32);
        ch |= g.Toggle("Locked", "Turn off, then drag the icons on screen to move them.", locked_);
        ch |= g.Slider("Horizontal position", "Pixels from the left edge of the screen.", x_, 0.0f, ctx.layout.screenW - 40.0f, "%.0f");
        ch |= g.Slider("Vertical position", "Pixels from the top of the screen.", y_, 0.0f, ctx.layout.screenH - 40.0f, "%.0f");
        if (g.Button("Position", "Back to the top-left corner.", "Reset"))
            x_ = 6.0f, y_ = 6.0f, ch = true;
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
