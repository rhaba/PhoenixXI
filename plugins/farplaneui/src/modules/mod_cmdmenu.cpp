/**
 * Farplane UI - moves the game's own command menu (and its Magic / Abilities sub-windows) to a spot
 * you choose, shown as a draggable "COMMAND MENU" frame while editing.
 *
 * The game positions a window through its own set-position routine (thiscall: window, x, y), which
 * stores x / y as int16 at window+0x52 / +0x54 and then re-lays the window out; writing those fields
 * directly does nothing visible. Each frame we walk the game's window list and call that routine for
 * the command windows, the approach xiuinative uses (mechanism re-implemented here):
 *   UI manager   sig 32DBB9????????E8????????85C08944240C74??8D44240CB9????????50E8????????8BF0, imm32 at +3;
 *                its UI width / height are int16 at +0x80 / +0x82; *manager = first list node
 *   list node    next = *(node+0), window = *(node+0x10), skip if byte node+0x14 != 0
 *   window       byte +0x70 == 0, header = *(window+4) with the 16-byte name "menu    <name8>" at +0x46,
 *                *(*(window+0xC)+8) == window, rect (int16 left, top, right, bottom) at +0x42
 *   set position sig 83EC0C668B44241453558B6C2418568BF15733FF8D5E1466896E528BCB66894654897C2414897C2418E8
 * The command windows move together: the group's left edge goes to the frame's x and each window's
 * bottom to the frame's bottom (screen pixels scaled to the game's UI size). Original positions are
 * remembered per window and restored when disabled or unloaded.
 * "Move dialog menus": any other game menu that opens over the chat panel (yes / no boxes, the Moogle's
 * job list, event choices) moves to the same frame as one group, its bottom-left on the frame's
 * bottom-left. Decided once per window from where the game first put it; the game's own log and input
 * windows never move.
 */
#include "common.hpp"

#include <cmath>
#include <cstring>

namespace farplaneui
{
    namespace
    {
        using SetPos_f = void(__thiscall*)(void* window, int x, int y);

        auto ModuleRange(uintptr_t& base, uintptr_t& size) -> bool
        {
            const HMODULE m = ::GetModuleHandleA("FFXiMain.dll");
            if (m == nullptr)
                return false;
            base            = reinterpret_cast<uintptr_t>(m);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<uintptr_t>(dos->e_lfanew));
            size            = nt->OptionalHeader.SizeOfImage;
            return true;
        }
        // A pattern that matches exactly once (0 otherwise).
        auto Unique(uintptr_t base, uintptr_t size, const char* pattern) -> uintptr_t
        {
            const uintptr_t at = Ashita::Memory::FindPattern(base, size, pattern, 0, 0);
            return at != 0 && Ashita::Memory::FindPattern(base, size, pattern, 0, 1) == 0 ? at : 0;
        }
        template <typename T>
        auto Rd(uintptr_t a) -> T
        {
            return a != 0 ? Ashita::Memory::SafeRead<T>(a, T{}) : T{};
        }
        // The game routine can fault on a window that is being torn down; never let that take the client.
        auto CallSetPos(SetPos_f fn, uintptr_t window, int x, int y) -> bool
        {
            __try
            {
                fn(reinterpret_cast<void*>(window), x, y);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        constexpr const char* kCommandWindows[] = {"playermo", "magic   ", "ability ", "magselec", "abiselec", "mount   "};
        // The game's chat log / input and HUD windows: never moved as dialogs.
        // Windows the game places from the bottom of the screen: their y (rect and set-position) counts up from the
        // bottom edge (the Moogle's main / sub job list reads 48..234 and draws at 846..1032 of 1080). Moved with
        // the y flipped: bottom edge on the frame's bottom.
        constexpr const char* kFromBottom[] = {"jobcselu"};
        auto FromBottom(const char* name8) -> bool
        {
            for (const char* c : kFromBottom)
                if (std::strncmp(name8, c, 8) == 0)
                    return true;
            return false;
        }
        constexpr const char* kNeverMove[] = {"logwindo", "inline  ", "fulllog ", "buff    ", "compass ", "casttime", "targetwi", "party   ", "partywin"};
    } // namespace

    void CommandMenuModule::Scan(Context& ctx)
    {
        scanned_ = true;
        uintptr_t base = 0, size = 0;
        if (!ModuleRange(base, size))
            return;
        setPos_ = Unique(base, size, "83EC0C668B44241453558B6C2418568BF15733FF8D5E1466896E528BCB66894654897C2414897C2418E8");
        if (const uintptr_t m = Unique(base, size, "32DBB9????????E8????????85C08944240C74??8D44240CB9????????50E8????????8BF0"); m != 0)
            manager_ = Rd<uint32_t>(m + 3);
        if (setPos_ == 0 || manager_ == 0)
            ctx.Print("Command menu: game signatures not found (client version?); the command menu stays in place.");
    }

    // The command windows currently open: (window, rect).
    auto CommandMenuModule::Windows(void) const -> std::vector<Win>
    {
        std::vector<Win> out;
        uintptr_t node = Rd<uint32_t>(manager_);
        for (int guard = 0; node != 0 && guard < 512; ++guard, node = Rd<uint32_t>(node))
        {
            if (Rd<uint8_t>(node + 0x14) != 0)
                continue;
            const uintptr_t win = Rd<uint32_t>(node + 0x10);
            if (win == 0 || Rd<uint8_t>(win + 0x70) != 0)
                continue;
            const uintptr_t hdr = Rd<uint32_t>(win + 4);
            const uintptr_t own = Rd<uint32_t>(win + 0xC);
            if (hdr == 0 || own == 0 || Rd<uint32_t>(own + 8) != win)
                continue;
            char name[17]{};
            for (int i = 0; i < 16; ++i)
                name[i] = static_cast<char>(Rd<uint8_t>(hdr + 0x46 + static_cast<uintptr_t>(i)));
            if (std::strncmp(name, "menu    ", 8) != 0)
                continue;
            bool command = false, never = false;
            for (const char* c : kCommandWindows)
                command |= std::strncmp(name + 8, c, 8) == 0;
            for (const char* c : kNeverMove)
                never |= std::strncmp(name + 8, c, 8) == 0;
            if (never)
                continue;
            Win w{win, Rd<int16_t>(win + 0x42), Rd<int16_t>(win + 0x44), Rd<int16_t>(win + 0x46), Rd<int16_t>(win + 0x48), command, {}};
            std::memcpy(w.name, name + 8, 8);
            if (w.right > w.left && w.bottom > w.top)
                out.push_back(w);
        }
        return out;
    }

    void CommandMenuModule::Frame(Context& ctx)
    {
        if (!scanned_)
            Scan(ctx);
        ctx.commandMenuMoved = enabled_ && setPos_ != 0 && manager_ != 0;
        ctx.dialogsMoved     = ctx.commandMenuMoved && dialogs_;
        if (setPos_ == 0 || manager_ == 0)
            return;
        auto* party = ctx.core->GetMemoryManager() != nullptr ? ctx.core->GetMemoryManager()->GetParty() : nullptr;
        const bool loggedIn = party != nullptr && party->GetMemberName(0) != nullptr && party->GetMemberName(0)[0] != '\0';
        const auto fn = reinterpret_cast<SetPos_f>(setPos_);
        const auto all = loggedIn ? Windows() : std::vector<Win>{};
        // Screen pixels <-> the game's UI coordinates.
        const int uiW = Rd<int16_t>(manager_ + 0x80), uiH = Rd<int16_t>(manager_ + 0x82);
        const float sx = uiW >= 320 && ctx.layout.screenW > 0 ? uiW / ctx.layout.screenW : 1.0f;
        const float sy = uiH >= 240 && ctx.layout.screenH > 0 ? uiH / ctx.layout.screenH : 1.0f;
        // A non-command menu is a "dialog" if it first opened over the chat panel (decided once per window).
        const Rect& chat = ctx.layout.chat;
        const float cl = chat.x * sx, ct = chat.y * sy, cr = (chat.x + chat.w) * sx, cb = (chat.y + chat.h) * sy;
        for (const auto& w : all)
            if (!w.command && dialog_.find(w.window) == dialog_.end())
                dialog_[w.window] = FromBottom(w.name) || (chat.valid() && w.left < cr && w.right > cl && w.top < cb && w.bottom > ct);
        std::vector<Win> wins, dialogs, fromBottom;
        for (const auto& w : all)
            if (w.command)
                wins.push_back(w);
            else if (dialogs_ && dialog_[w.window])
                (FromBottom(w.name) ? fromBottom : dialogs).push_back(w);
        if (!log_.empty())
        {
            std::string line;
            for (const auto& w : all)
            {
                char b[200]{};
                ::sprintf_s(b, " [%s %d,%d-%d,%d pos %d,%d %s]", w.name, w.left, w.top, w.right, w.bottom, Rd<int16_t>(w.window + 0x52),
                    Rd<int16_t>(w.window + 0x54),
                    w.command ? "command" : dialog_[w.window] ? (dialogs_ ? "dialog: moved" : "dialog (moving off)") : "left alone");
                line += b;
            }
            char head[96]{};
            ::sprintf_s(head, "UI %dx%d, chat %.0f,%.0f-%.0f,%.0f (UI):", uiW, uiH, static_cast<double>(cl), static_cast<double>(ct), static_cast<double>(cr),
                static_cast<double>(cb));
            line = head + line;
            if (line != lastLog_)
            {
                lastLog_ = line;
                FILE* f = nullptr;
                if (::fopen_s(&f, log_.c_str(), "ab") == 0 && f != nullptr)
                {
                    ::fprintf(f, "%llu  %s\r\n", static_cast<unsigned long long>(::GetTickCount64()), line.c_str());
                    ::fclose(f);
                }
            }
        }

        // Forget windows that closed.
        for (auto it = dialog_.begin(); it != dialog_.end();)
        {
            bool open = false;
            for (const auto& w : all)
                open |= w.window == it->first;
            it = open ? std::next(it) : dialog_.erase(it);
        }
        for (auto it = orig_.begin(); it != orig_.end();)
        {
            bool open = false;
            for (const auto& w : all)
                open |= w.window == it->first;
            it = open ? std::next(it) : orig_.erase(it);
        }
        for (const auto& w : dialogs)
            if (orig_.find(w.window) == orig_.end())
                orig_[w.window] = Rd<uint32_t>(w.window + 0x52);
        // Dialogs: one group, bottom-left on the frame's bottom-left (or back where the game had them).
        if (!dialogs.empty())
        {
            if (!enabled_ || user_.x < 0)
            {
                for (const auto& w : dialogs)
                    if (Rd<uint32_t>(w.window + 0x52) != orig_[w.window])
                    {
                        const uint32_t o = orig_[w.window];
                        CallSetPos(fn, w.window, static_cast<int16_t>(o & 0xFFFF), static_cast<int16_t>(o >> 16));
                    }
            }
            else
            {
                int left = 32767, bottom = -32768;
                for (const auto& w : dialogs)
                    left = std::min<int>(left, w.left), bottom = std::max<int>(bottom, w.bottom);
                const int dx = static_cast<int>(std::lround(std::clamp(user_.x * sx - left, -16000.0f, 16000.0f)));
                const int dy = static_cast<int>(std::lround(std::clamp((user_.y + user_.h) * sy - bottom, -16000.0f, 16000.0f)));
                if (dx != 0 || dy != 0)
                    for (const auto& w : dialogs)
                        CallSetPos(fn, w.window, w.left + dx, w.top + dy);
            }
        }
        // Bottom-placed windows: x to the frame's left, the bottom edge (their y, counted from the bottom) on the frame's bottom.
        for (const auto& w : fromBottom)
        {
            if (orig_.find(w.window) == orig_.end())
                orig_[w.window] = Rd<uint32_t>(w.window + 0x52);
            if (!enabled_ || user_.x < 0)
            {
                const uint32_t o = orig_[w.window];
                if (Rd<uint32_t>(w.window + 0x52) != o)
                    CallSetPos(fn, w.window, static_cast<int16_t>(o & 0xFFFF), static_cast<int16_t>(o >> 16));
                continue;
            }
            const int nx = static_cast<int>(std::lround(std::clamp(user_.x * sx, -16000.0f, 16000.0f)));
            const int ny = static_cast<int>(std::lround(std::clamp(uiH - (user_.y + user_.h) * sy, -16000.0f, 16000.0f)));
            if (w.left != nx || w.top != ny)
                CallSetPos(fn, w.window, nx, ny);
        }
        // Dialogs the option was turned off for: put back.
        if (!dialogs_)
            for (const auto& w : all)
                if (!w.command && orig_.find(w.window) != orig_.end() && Rd<uint32_t>(w.window + 0x52) != orig_[w.window])
                {
                    const uint32_t o = orig_[w.window];
                    CallSetPos(fn, w.window, static_cast<int16_t>(o & 0xFFFF), static_cast<int16_t>(o >> 16));
                }
        if (wins.empty())
            return;
        for (const auto& w : wins)
            if (orig_.find(w.window) == orig_.end())
                orig_[w.window] = Rd<uint32_t>(w.window + 0x52); // x | y << 16, as the game had it

        if (!enabled_ || user_.x < 0)
        {
            // Off: put every window we moved back where the game had it.
            for (const auto& w : wins)
                if (Rd<uint32_t>(w.window + 0x52) != orig_[w.window])
                {
                    const uint32_t o = orig_[w.window];
                    CallSetPos(fn, w.window, static_cast<int16_t>(o & 0xFFFF), static_cast<int16_t>(o >> 16));
                }
            return;
        }
        int minLeft = 32767;
        for (const auto& w : wins)
            minLeft = std::min<int>(minLeft, w.left);
        const int dx     = static_cast<int>(std::lround(std::clamp(user_.x * sx - minLeft, -16000.0f, 16000.0f)));
        const int bottom = static_cast<int>(std::lround(std::clamp((user_.y + user_.h) * sy, -16000.0f, 16000.0f)));
        for (const auto& w : wins)
        {
            const int nx = w.left + dx, ny = bottom - (w.bottom - w.top);
            const uint32_t want = static_cast<uint16_t>(nx) | (static_cast<uint32_t>(static_cast<uint16_t>(ny)) << 16);
            if (Rd<uint32_t>(w.window + 0x52) != want)
                CallSetPos(fn, w.window, nx, ny);
        }
    }

    void CommandMenuModule::Release(void)
    {
        // Restore any open command window we moved.
        if (setPos_ == 0)
            return;
        const auto fn = reinterpret_cast<SetPos_f>(setPos_);
        for (const auto& [win, o] : orig_)
            if (Rd<uint32_t>(win + 0x52) != o)
                CallSetPos(fn, win, static_cast<int16_t>(o & 0xFFFF), static_cast<int16_t>(o >> 16));
        orig_.clear();
    }

    // While editing (settings open on this module, or /fpui cmdmenu edit) a dashed frame marks where the
    // command menu will open; drag it to move.
    void CommandMenuModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (user_.x < 0) // default: left edge, bottom above the compass / chat
            user_ = Rect{16.0f, std::max(40.0f, ctx.layout.bandTop - 150.0f - 220.0f), 150.0f, 220.0f};
        if (!editing_ && !ctx.settingsOpen)
            return;
        if (!editing_ && ctx.settingsModule != this)
            return;
        gui->SetNextWindowPos(ImVec2(user_.x, user_.y), ImGuiCond_Appearing);
        gui->SetNextWindowSize(ImVec2(user_.w, user_.h), ImGuiCond_Appearing);
        gui->PushStyleColor(ImGuiCol_WindowBg, ToVec4(Rgba{art::twilight_dark.r, art::twilight_dark.g, art::twilight_dark.b, 0.55f}));
        gui->PushStyleColor(ImGuiCol_Border, ToVec4(art::ember));
        if (gui->Begin("##fpui_cmdmenu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
        {
            const ImVec2 p = gui->GetWindowPos(), s = gui->GetWindowSize();
            const char* label = "COMMAND MENU";
            const ImVec2 ts = gui->CalcTextSize(label);
            gui->SetCursorScreenPos(ImVec2(p.x + (s.x - ts.x) * 0.5f, p.y + (s.y - ts.y) * 0.5f));
            gui->TextColored(ToVec4(art::gold_hi), "%s", label);
            if (ui::ReadBack(gui, user_))
                ctx.MarkDirty();
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
        gui->PopStyleColor(2);
    }

    void CommandMenuModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        enabled_ = cfg->GetBool(alias, "cmdmenu", "enabled", enabled_);
        dialogs_ = cfg->GetBool(alias, "cmdmenu", "dialogs", dialogs_);
        ui::LoadRect(cfg, alias, "cmdmenu", "frame", user_);
    }
    void CommandMenuModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        ui::SetB(cfg, alias, "cmdmenu", "enabled", enabled_);
        ui::SetB(cfg, alias, "cmdmenu", "dialogs", dialogs_);
        ui::SaveRect(cfg, alias, "cmdmenu", "frame", user_);
    }
    auto CommandMenuModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "on" || sub == "off")
            enabled_ = sub == "on";
        else if (sub == "edit")
        {
            editing_ = !editing_;
            ctx.Print(editing_ ? "Command menu: drag the COMMAND MENU frame, then /fpui cmdmenu edit again." : "Command menu: position saved.");
        }
        else if (sub == "reset")
            user_.x = -1;
        else if (sub == "dialogs")
            dialogs_ = args.size() > 1 ? ui::Lower(args[1]) != "off" : !dialogs_;
        else if (sub == "log")
        {
            if (log_.empty())
            {
                const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
                ::CreateDirectoryA(dir.c_str(), nullptr);
                log_ = dir + "\\farplaneui_cmdmenu.log";
                ::DeleteFileA(log_.c_str());
                lastLog_.clear();
                ctx.Print("Command menu: logging the game's open windows to " + log_ + ". /fpui cmdmenu log again to stop.");
            }
            else
            {
                ctx.Print("Command menu: log off. Log: " + log_);
                log_.clear();
            }
            return true;
        }
        else if (sub == "status")
        {
            ctx.Print(std::string("Command menu: ") + (enabled_ ? "on" : "off") + "; set-position " + (setPos_ != 0 ? "found" : "NOT found") +
                      "; window list " + (manager_ != 0 ? "found" : "NOT found") + "; " + std::to_string(Windows().size()) + " command window(s) open.");
            return true;
        }
        else
        {
            ctx.Print("/fpui cmdmenu [on|off] | dialogs [on|off] | edit (drag the frame) | reset | status | log");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }
    void CommandMenuModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Move command menu", "Open the game's command menu (and Magic / Abilities lists) at the COMMAND MENU frame.", enabled_);
        ch |= g.Toggle("Move dialog menus", "Also move any other game menu that would open over the chat (yes / no boxes, the Moogle's job list, event "
                                             "choices) to the same frame, so the chat stays readable.", dialogs_);
        if (g.Button("Position", "Drag the COMMAND MENU frame shown while this page is open. Its bottom-left is where the menu opens.", "Reset"))
        {
            user_.x = -1;
            ch      = true;
        }
        g.Info("Game hooks", setPos_ != 0 && manager_ != 0 ? "Found." : "Not found - the command menu stays where the game puts it.");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
