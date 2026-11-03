/**
 * Farplane UI - controller support for FarplaneUI's own menus (see pad.hpp).
 *
 * Input comes from Ashita's DirectInput controller hook: one call per value, offset 32 = the D-pad hat
 * (hundredths of a degree, -1 = centred), 48 + n = button n (128 down, 0 up), 0 / 4 = the left stick.
 * Each action is a button number from [pad] (defaults: a DualShock-style pad as reported on PhoenixXI:
 * 1 confirm, 2 cancel, 4 / 5 L1 / R1, 6 / 7 L2 / R2, 13 touchpad). While FarplaneUI holds the controller
 * those buttons, the hat and the left stick are blocked from the game (the callback returns TRUE).
 *
 * /fpui pad log on|off writes the raw button events and the open game menu's name and changing values
 * to <Ashita>\logs\farplaneui_pad.log (used to find the numbering and the status icon cursor).
 * /fpui pad bind <action> assigns the next button pressed to an action.
 */
#include "common.hpp"
#include "../pad.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace farplaneui
{
    namespace
    {
        PadModule* g_pad = nullptr;
        std::mutex g_padLock;
        std::atomic<uint32_t> g_buttons{0}; // bit n = button n down
        std::atomic<int32_t> g_hat{-1};      // D-pad, hundredths of a degree
        std::atomic<bool> g_capture{false};
        std::atomic<int> g_bindAction{-1};   // /fpui pad bind: next button goes to this action
        std::atomic<int> g_bound{-1};        // the button that was just bound

        constexpr const char* kActionKeys[pad::kActions]  = {"up", "down", "left", "right", "confirm", "cancel", "prevpage", "nextpage", "prevsection", "nextsection", "open"};
        constexpr const char* kActionNames[pad::kActions] = {"Up", "Down", "Left", "Right", "Confirm", "Cancel", "Previous category", "Next category",
            "Previous sub-tab", "Next sub-tab", "Open / close menu"};

        auto NowS(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        // D-pad direction from the hat (diagonals count for both).
        auto HatHas(int32_t hat, int dir) -> bool
        {
            if (hat < 0 || hat >= 36000)
                return false;
            switch (dir)
            {
            case pad::Up: return hat >= 31500 || hat <= 4500;
            case pad::Right: return hat >= 4500 && hat <= 13500;
            case pad::Down: return hat >= 13500 && hat <= 22500;
            case pad::Left: return hat >= 22500 && hat <= 31500;
            default: return false;
            }
        }
    } // namespace

    // --- pad.hpp
    namespace pad
    {
        auto Pressed(Action a) -> bool { return g_pad != nullptr && a >= 0 && a < kActions && g_pad->pressed_[a]; }
        auto HeldFor(Action a) -> float
        {
            if (g_pad == nullptr || a < 0 || a >= kActions || g_pad->downAt_[a] <= 0)
                return 0.0f;
            return static_cast<float>(NowS() - g_pad->downAt_[a]);
        }
        void SetCapture(bool on) { g_capture = on; }
        auto Captured(void) -> bool { return g_capture; }
    } // namespace pad

    // Confirm 1, cancel 2 (the game's own on this pad, from /fpui pad log: 1 opens every menu, 2 backs out).
    PadModule::PadModule(void) : buttons_{-1, -1, -1, -1, 1, 2, 4, 5, 6, 7, 13} {}

    void PadModule::Initialize(Context& ctx)
    {
        core_ = ctx.core;
        g_pad = this;
        auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr;
        if (im == nullptr)
            return;
        if (auto* c = im->GetController(); c != nullptr)
        {
            const getdevicedatacallback_f dcb  = &PadModule::DDataCallback;
            const getdevicestatecallback_f scb = &PadModule::DStateCallback;
            const controllercallback_f ccb     = &PadModule::DButtonCallback;
            c->AddCallback("farplaneui_pad", dcb, scb, ccb);
        }
    }
    void PadModule::Release(void)
    {
        auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr;
        if (im != nullptr)
            if (auto* c = im->GetController(); c != nullptr)
                c->RemoveCallback("farplaneui_pad");
        g_capture = false;
        g_pad     = nullptr;
    }

    void PadModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "pad";
        enabled_ = cfg->GetBool(alias, S, "enabled", enabled_);
        // rev 1 saved confirm 2 / cancel 1 (swapped): those two take the new defaults.
        const int rev = cfg->GetInt32(alias, S, "rev", 1);
        for (int a = pad::Confirm; a < pad::kActions; ++a)
            if (rev >= 2 || (a != pad::Confirm && a != pad::Cancel))
                buttons_[static_cast<size_t>(a)] = std::clamp(cfg->GetInt32(alias, S, kActionKeys[a], buttons_[static_cast<size_t>(a)]), 0, 31);
    }
    void PadModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "pad";
        ui::SetB(cfg, alias, S, "enabled", enabled_);
        ui::SetI(cfg, alias, S, "rev", 2);
        for (int a = pad::Confirm; a < pad::kActions; ++a)
            ui::SetI(cfg, alias, S, kActionKeys[a], buttons_[static_cast<size_t>(a)]);
    }

    // One line per event with a millisecond tick (written from Frame; the callbacks only queue).
    void PadModule::Write(const std::string& line) const
    {
        if (logFile_.empty())
            return;
        FILE* f = nullptr;
        if (::fopen_s(&f, logFile_.c_str(), "ab") == 0 && f != nullptr)
        {
            ::fprintf(f, "%llu  %s\r\n", static_cast<unsigned long long>(::GetTickCount64()), line.c_str());
            ::fclose(f);
        }
    }
    void PadModule::Queue(const std::string& line)
    {
        std::lock_guard<std::mutex> l(g_padLock);
        if (pending_.size() < 5000)
            pending_.push_back(std::to_string(::GetTickCount64()) + "  " + line);
    }

    auto __stdcall PadModule::DStateCallback(LPDIRECTINPUTDEVICE8A, DWORD size, LPVOID) -> HRESULT
    {
        if (g_pad != nullptr)
            ++g_pad->dStates_, g_pad->dStateSize_ = size;
        return DI_OK;
    }
    auto __stdcall PadModule::DDataCallback(LPDIRECTINPUTDEVICE8A, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD, DWORD) -> HRESULT { return DI_OK; }

    // Every controller value: recorded for the menus, logged on request, blocked while FarplaneUI holds it.
    auto __stdcall PadModule::DButtonCallback(uint32_t* offset, int32_t* state, bool blocked, bool injected) -> BOOL
    {
        PadModule* p = g_pad;
        if (p == nullptr || offset == nullptr || state == nullptr || injected)
            return FALSE;
        const uint32_t ofs = *offset;
        const int32_t v    = *state;
        if (ofs == 32)
            g_hat = v;
        else if (ofs >= 48 && ofs < 48 + 32)
        {
            const uint32_t bit = 1u << (ofs - 48);
            const bool down    = v != 0;
            const uint32_t was = g_buttons.load();
            g_buttons          = down ? (was | bit) : (was & ~bit);
            if (down && (was & bit) == 0 && g_bindAction >= 0)
                g_bound = static_cast<int>(ofs - 48);
        }
        if (p->log_ && ofs >= 32) // buttons + D-pad, not the stick axes
        {
            char b[112]{};
            ::sprintf_s(b, "pad DirectInput offset %u (0x%X) state %d%s", static_cast<unsigned>(ofs), static_cast<unsigned>(ofs), static_cast<int>(v),
                blocked ? " (blocked)" : "");
            p->Queue(b);
        }
        if (!g_capture || !p->enabled_)
            return FALSE;
        // Held by FarplaneUI: the D-pad, every button and the left stick (no walking while in the menu).
        return (ofs == 32 || (ofs >= 48 && ofs < 48 + 32) || ofs == 0 || ofs == 4) ? TRUE : FALSE;
    }

    void PadModule::Frame(Context& ctx)
    {
        // --- actions: edges with repeat for the D-pad and left / right
        const double now      = NowS();
        const uint32_t mask   = enabled_ ? g_buttons.load() : 0u;
        const int32_t hat     = enabled_ ? g_hat.load() : -1;
        for (int a = 0; a < pad::kActions; ++a)
        {
            const bool down = a <= pad::Right ? HatHas(hat, a) : ((mask >> buttons_[static_cast<size_t>(a)]) & 1u) != 0;
            auto& at        = downAt_[static_cast<size_t>(a)];
            auto& next      = repeatAt_[static_cast<size_t>(a)];
            bool hit        = false;
            if (down && at <= 0)
                at = now, next = now + 0.38, hit = true;
            else if (down && a <= pad::Right && now >= next)
                next = now + (now - at > 1.5 ? 0.04 : 0.09), hit = true; // faster after a while
            else if (!down)
                at = 0;
            pressed_[static_cast<size_t>(a)] = hit;
        }
        // --- /fpui pad bind
        if (const int b = g_bound.exchange(-1); b >= 0 && g_bindAction >= 0)
        {
            const int a = g_bindAction.exchange(-1);
            buttons_[static_cast<size_t>(a)] = b;
            pressed_.fill(false);
            ctx.MarkDirty();
            ctx.Print(std::string("Controller: ") + kActionNames[a] + " = button " + std::to_string(b) + ".");
        }

        // --- log
        {
            std::lock_guard<std::mutex> l(g_padLock);
            if (!pending_.empty() && !logFile_.empty())
            {
                FILE* f = nullptr;
                if (::fopen_s(&f, logFile_.c_str(), "ab") == 0 && f != nullptr)
                {
                    for (const auto& s : pending_)
                        ::fprintf(f, "%s\r\n", s.c_str());
                    ::fclose(f);
                }
            }
            pending_.clear();
        }
        if (!log_)
            return;
        // Whatever game menu is on top: its name whenever it changes, and the values that change in its object
        // and header while it stays open (the status icon cursor's position is one of them).
        const std::string menu = mod::TopMenuName();
        if (menu != lastMenu_)
        {
            Write("menu now \"" + menu + "\"");
            lastMenu_ = menu;
            inBuff_   = false;
        }
        if (menu.empty())
        {
            inBuff_ = false;
            return;
        }
        const uintptr_t obj = mod::TopMenuObject(), hdr = mod::TopMenuHeader();
        std::array<uint32_t, 64> o{}, h{};
        for (size_t i = 0; i < o.size(); ++i)
        {
            o[i] = obj != 0 ? Ashita::Memory::SafeRead<uint32_t>(obj + i * 4, 0u) : 0u;
            h[i] = hdr != 0 ? Ashita::Memory::SafeRead<uint32_t>(hdr + i * 4, 0u) : 0u;
        }
        if (inBuff_)
            for (size_t i = 0; i < o.size(); ++i)
            {
                char b[96]{};
                if (o[i] != obj_[i])
                    ::sprintf_s(b, "menu obj+0x%02X: %u -> %u", static_cast<unsigned>(i * 4), obj_[i], o[i]), Write(b);
                if (h[i] != hdr_[i])
                    ::sprintf_s(b, "menu hdr+0x%02X: %u -> %u", static_cast<unsigned>(i * 4), hdr_[i], h[i]), Write(b);
            }
        obj_ = o, hdr_ = h, inBuff_ = true;
    }

    void PadModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Use a controller in FarplaneUI's menus. The touchpad (or your Open button) opens the settings; while they are open "
                                      "the controller drives them and the game doesn't see those buttons.", enabled_);
        g.Section("Buttons (DirectInput button numbers)");
        for (int a = pad::Confirm; a < pad::kActions; ++a)
        {
            char d[160]{};
            ::sprintf_s(d, "Button %d. Set it by pressing: /fpui pad bind %s", buttons_[static_cast<size_t>(a)], kActionKeys[a]);
            ch |= g.Slider(kActionNames[a], d, buttons_[static_cast<size_t>(a)], 0, 31);
        }
        g.Info("D-pad", "Move between settings (up / down) and change sliders and choices (left / right).");
        if (ch)
            ctx.MarkDirty();
    }

    auto PadModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        const std::string a1  = args.size() > 1 ? ui::Lower(args[1]) : std::string();
        if (sub == "log")
        {
            const bool was = log_;
            log_ = a1 == "on" ? true : a1 == "off" ? false : !log_;
            if (log_ && !was)
            {
                const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
                ::CreateDirectoryA(dir.c_str(), nullptr);
                logFile_ = dir + "\\farplaneui_pad.log";
                ::DeleteFileA(logFile_.c_str());
                lastMenu_ = "?";
                Write("FarplaneUI pad log");
            }
            if (!log_ && was)
            {
                char b[160]{};
                ::sprintf_s(b, "DirectInput state polls %u (last size %u)", dStates_, dStateSize_);
                Write(b);
            }
            ctx.Print(log_ ? "pad: logging controller buttons and game menus to " + logFile_ + ". /fpui pad log off to stop."
                           : "pad: logging off. Log: " + logFile_);
            return true;
        }
        if (sub == "bind")
        {
            for (int a = pad::Confirm; a < pad::kActions; ++a)
                if (a1 == kActionKeys[a])
                {
                    g_bound      = -1;
                    g_bindAction = a;
                    ctx.Print(std::string("Controller: press the button for ") + kActionNames[a] + ".");
                    return true;
                }
            ctx.Print("Controller: /fpui pad bind confirm|cancel|prevpage|nextpage|prevsection|nextsection|open");
            return true;
        }
        // /fpui pad game on|off: Ashita's switch for the game's own controller support (test: with the controller
        // off, does turning it off stop the game's periodic controller search and its hitches?).
        if (sub == "game")
        {
            auto* im = ctx.core != nullptr ? ctx.core->GetInputManager() : nullptr;
            if (im == nullptr)
                return true;
            if (a1 == "on" || a1 == "off")
                im->SetDisableGamepad(a1 == "off");
            ctx.Print(std::string("Controller: the game's controller support is ") + (im->GetDisableGamepad() ? "OFF" : "ON") +
                      " (Ashita). /fpui pad game on|off");
            return true;
        }
        if (sub == "on" || sub == "off")
        {
            enabled_ = sub == "on";
            ctx.MarkDirty();
        }
        char b[200]{};
        ::sprintf_s(b, "Controller %s: open %d, confirm %d, cancel %d, L1 %d, R1 %d, L2 %d, R2 %d. /fpui pad on|off | bind <action> | log on|off",
            enabled_ ? "on" : "off", buttons_[pad::Open], buttons_[pad::Confirm], buttons_[pad::Cancel], buttons_[pad::PrevPage], buttons_[pad::NextPage],
            buttons_[pad::PrevSection], buttons_[pad::NextSection]);
        ctx.Print(b);
        return true;
    }
} // namespace farplaneui
