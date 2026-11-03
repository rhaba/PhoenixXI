/**
 * Farplane UI - player status effects (top-left, where the game draws its own).
 *
 * The player's status icons in game order, each with its time left underneath in the native style
 * ("5h", "15m", "42"). Read from memory through mod::PlayerBuffs; hovering an icon shows its name.
 * Right-clicking a cancellable effect cancels it, the same request the statustimers addon sends
 * (packet 0x0F1 with the status id; the game's own "cancel" for an effect).
 * The game's own status icon cursor (keyboard F / controller, game menu "menu    buff", position 1..n at
 * menu object +0x4C) is mirrored here: its icon is framed, and confirm (the controller's Confirm button or
 * Enter) cancels it - the game's icons are hidden, and a hidden window doesn't take the game's confirm.
 */
#include "common.hpp"
#include "../pad.hpp"

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
        // The game marks which effects the player may cancel (IStatusIcon::CanCancel; statustimers' check).
        auto CanCancel(Context& ctx, uint16_t id) -> bool
        {
            if (id == 0 || id >= 0x400 || id == 255)
                return false;
            auto* rm = ctx.core->GetResourceManager();
            const IStatusIcon* s = rm != nullptr ? rm->GetStatusIconByIndex(id) : nullptr;
            return s != nullptr && s->CanCancel != 0;
        }
        void Cancel(Context& ctx, uint16_t id)
        {
            // 0x0F1 (8 bytes): header (id, size in 4-byte words, sync), then the status id at +4.
            uint8_t p[8] = {0xF1, 0x04, 0, 0, static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8), 0, 0};
            ctx.core->GetPacketManager()->AddOutgoingPacket(0xF1, sizeof(p), p);
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
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
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
            // The game's status icon cursor: which of our icons it is on, and confirm = cancel it.
            int cursor = -1;
            if (mod::TopMenuName() == "menu    buff")
            {
                const uintptr_t obj = mod::TopMenuObject();
                cursor = obj != 0 ? static_cast<int>(Ashita::Memory::SafeRead<uint32_t>(obj + 0x4C, 0u)) - 1 : -1;
                if (obj != 0 && cursor != lastCursor_) // /fpui status align: the game's window rect per cursor move, to a file
                    LogAlign(ctx, obj, cursor, count);
                lastCursor_ = cursor;
                const bool enter = (::GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
                const bool confirm = pad::Pressed(pad::Confirm) || (enter && !enterDown_);
                enterDown_ = enter;
                if (confirm && cursor >= 0 && cursor < count && CanCancel(ctx, buffs[static_cast<size_t>(cursor)].id))
                    Cancel(ctx, buffs[static_cast<size_t>(cursor)].id);
            }
            else
                enterDown_ = (::GetAsyncKeyState(VK_RETURN) & 0x8000) != 0, lastCursor_ = -2;
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
                    const bool cancel = CanCancel(ctx, b.id);
                    const char* hint  = cancel ? "\n(right click to cancel)" : "";
                    if (b.seconds > 0)
                        gui->SetTooltip("%s  (%s)%s", mod::BuffName(ctx, b.id).c_str(), Long(b.seconds).c_str(), hint);
                    else
                        gui->SetTooltip("%s%s", mod::BuffName(ctx, b.id).c_str(), hint);
                    if (cancel && gui->IsMouseClicked(ImGuiMouseButton_Right))
                        Cancel(ctx, b.id);
                }
            }
            gui->PopFont();
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    // The game's (hidden) status window and cursor, next to our own layout: lines our icons up with the game's
    // selector. int16 words of the menu object around its rect (+0x42 x1, +0x44 y1, +0x46 x2, +0x48 y2).
    void StatusModule::LogAlign(Context& ctx, uintptr_t obj, int cursor, int count) const
    {
        if (alignLog_.empty())
            return;
        FILE* f = nullptr;
        if (::fopen_s(&f, alignLog_.c_str(), "ab") != 0 || f == nullptr)
            return;
        ::fprintf(f, "%llu  cursor %d of %d icons; ours at %.0f,%.0f size %.0f gap 3 perRow %d; screen %.0fx%.0f\r\n",
            static_cast<unsigned long long>(::GetTickCount64()), cursor + 1, count, static_cast<double>(x_), static_cast<double>(y_),
            static_cast<double>(iconSize_), perRow_, static_cast<double>(ctx.layout.screenW), static_cast<double>(ctx.layout.screenH));
        ::fprintf(f, "    obj int16 +0x38..+0x5E:");
        for (uintptr_t o = 0x38; o < 0x60; o += 2)
            ::fprintf(f, " %d", static_cast<int>(Ashita::Memory::SafeRead<int16_t>(obj + o, static_cast<int16_t>(0))));
        ::fprintf(f, "\r\n");
        ::fclose(f);
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
        if (sub == "align")
        {
            if (alignLog_.empty())
            {
                const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
                ::CreateDirectoryA(dir.c_str(), nullptr);
                alignLog_ = dir + "\\farplaneui_status.log";
                ::DeleteFileA(alignLog_.c_str());
                ctx.Print("Status: logging the game's status window to " + alignLog_ + " while its cursor moves. /fpui status align again to stop.");
            }
            else
            {
                ctx.Print("Status: align log off. Log: " + alignLog_);
                alignLog_.clear();
            }
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
