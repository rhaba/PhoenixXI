/**
 * Farplane UI - hides the native FFXI UI elements that FarplaneUI replaces.
 *
 * Party / alliance frames and the target frame use the method from atom0s's official Ashita v4
 * addon "hideparty" (https://github.com/AshitaXI/Ashita-v4beta/tree/main/addons/hideparty):
 *   sig1 "66C78182000000????C7818C000000????????C781900000" -> party0 = *(sig1+0x19), target = *(sig1+0x23)
 *   sig2 "A1????????8B0D????????89442424A1????????33DB89"   -> party1 = *(sig2+0x01), party2 = *(sig2+0x07)
 *   each is a pointer to a game UI object; obj = *ptr, prim = *(obj+0x08); visibility bytes at
 *   prim+0x69 and prim+0x6A (1 = shown, 0 = hidden). The game resets them, so they are written
 *   every frame, exactly like the addon's d3d_present handler.
 * Re-implemented in C++ (no code copied); only the published signatures/offsets are reused.
 * Restored to visible on toggle-off and in Release().
 *
 * Other native windows by name (status icons, cast bar, the "Target" window): the client keeps a
 * static table of its menus, each entry the 16-byte name "menu    <name8>" followed by 16 zero bytes
 * and, at +0x20, the address of the slot holding that menu's object (the lookup XIUI uses). The
 * object takes the same visibility bytes as the party frames above. The clock is toggled with the
 * game's own /clock off and /clock on.
 *
 * Native compass: not a menu in the table. The client decides whether to draw it from a one-byte
 * immediate in its HUD code: sig "33C0668B81????????483DE3000000", byte at +0x24 is 1 (draw) and
 * 0 hides it - the switch xiuinative's Game HUD uses for its compass control (it flips the same byte
 * with VirtualProtect + FlushInstructionCache and restores 1). Restored on toggle-off and unload.
 *
 * Native chat log WINDOW: method from the FancyChat addon (ariel-logos, GPL-3,
 * https://github.com/ariel-logos/Fancychat, lib/lifecycle.lua + lib/render.lua):
 *   sig "A1????????C64059018B0D????????C6415901C20800" (the client's message-window draw routine)
 *   win1 = *(sig+0x01), win2 = *(sig+0x0B); each frame obj = *win, write uint32 0 at obj+0x34
 *   (the log window's stay-open timer), which keeps the native log collapsed (frame + old text
 *   gone) while the chat input line keeps working. Turning it off just stops the writes; the
 *   game's own timer then behaves normally again (nothing to restore).
 */
#include "common.hpp"

#include <cstring>

namespace farplaneui
{
    namespace
    {
        auto Writable(uintptr_t addr, size_t n) -> bool
        {
            if (addr == 0)
                return false;
            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
                return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0 || (mbi.Protect & PAGE_GUARD) != 0)
                return false;
            return addr + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        }
        auto ReadU32(uintptr_t addr) -> uintptr_t
        {
            return addr != 0 ? Ashita::Memory::SafeRead<uint32_t>(addr, 0u) : 0u;
        }
        void SetVisible(uintptr_t pp, uint8_t v)
        {
            if (pp == 0)
                return;
            const uintptr_t obj = ReadU32(pp);
            if (obj == 0)
                return;
            const uintptr_t prim = ReadU32(obj + 0x08);
            if (prim == 0 || !Writable(prim + 0x69, 2))
                return;
            *reinterpret_cast<volatile uint8_t*>(prim + 0x69) = v;
            *reinterpret_cast<volatile uint8_t*>(prim + 0x6A) = v;
        }
        // Slot address of the client's "menu    <name>" window (0 if not found or not unique).
        auto MenuSlot(uintptr_t base, uintptr_t size, const char* name) -> uintptr_t
        {
            char id[17]{};
            ::sprintf_s(id, "menu    %-8.8s", name);
            std::string pattern;
            char hex[4]{};
            for (int i = 0; i < 32; ++i)
            {
                ::sprintf_s(hex, "%02X", i < 16 ? static_cast<unsigned>(static_cast<uint8_t>(id[i])) : 0u);
                pattern += hex;
            }
            const uintptr_t at = Ashita::Memory::FindPattern(base, size, pattern.c_str(), 0, 0);
            if (at == 0 || Ashita::Memory::FindPattern(base, size, pattern.c_str(), 0, 1) != 0)
                return 0;
            return ReadU32(at + 0x20);
        }
        // One byte of game code (only when it differs, so this is cheap every frame).
        void PatchByte(uintptr_t at, uint8_t v)
        {
            if (at == 0 || *reinterpret_cast<volatile uint8_t*>(at) == v)
                return;
            DWORD old = 0;
            if (!::VirtualProtect(reinterpret_cast<void*>(at), 1, PAGE_EXECUTE_READWRITE, &old))
                return;
            *reinterpret_cast<volatile uint8_t*>(at) = v;
            ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(at), 1);
            ::VirtualProtect(reinterpret_cast<void*>(at), 1, old, &old);
        }
        void PinChatClosed(uintptr_t pp)
        {
            if (pp == 0)
                return;
            const uintptr_t obj = ReadU32(pp);
            if (obj == 0 || !Writable(obj + 0x34, 4))
                return;
            *reinterpret_cast<volatile uint32_t*>(obj + 0x34) = 0;
        }
    } // namespace

    void NativeUiModule::Scan(Context& ctx)
    {
        scanned_         = true;
        const HMODULE m  = ::GetModuleHandleA("FFXiMain.dll");
        if (m == nullptr)
        {
            ctx.Print("Native UI: FFXiMain.dll not found; party/target frames will stay visible.");
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(m);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<uintptr_t>(dos->e_lfanew));
        const uintptr_t size = nt->OptionalHeader.SizeOfImage;
        const uintptr_t sc = Ashita::Memory::FindPattern(base, size, "A1????????C64059018B0D????????C6415901C20800", 0, 0);
        if (sc != 0)
        {
            chatWin1_  = sc + 0x01;
            chatWin2_  = sc + 0x0B;
            chatFound_ = true;
        }
        else
            ctx.Print("Native UI: chat window signature not found (client version?); native chat log frame stays visible.");
        {
            const char* sig    = "33C0668B81????????483DE3000000";
            const uintptr_t at = Ashita::Memory::FindPattern(base, size, sig, 0, 0);
            if (at != 0 && Ashita::Memory::FindPattern(base, size, sig, 0, 1) == 0)
            {
                const uint8_t b = Ashita::Memory::SafeRead<uint8_t>(at + 0x24, 0xFF);
                if (b == 0 || b == 1) // anything else: not the byte we expect, leave the code alone
                    compassByte_ = at + 0x24;
            }
        }
        // Game UI manager (live window list), as in mod_cmdmenu.cpp: imm32 at +3.
        if (const uintptr_t mgr = Ashita::Memory::FindPattern(base, size, "32DBB9????????E8????????85C08944240C74??8D44240CB9????????50E8????????8BF0", 0, 0);
            mgr != 0)
            manager_ = ReadU32(mgr + 3);
        const uintptr_t s1 = Ashita::Memory::FindPattern(base, size, "66C78182000000????C7818C000000????????C781900000", 0, 0);
        const uintptr_t s2 = Ashita::Memory::FindPattern(base, size, "A1????????8B0D????????89442424A1????????33DB89", 0, 0);
        compass_   = MenuSlot(base, size, "compass");
        buffs_     = MenuSlot(base, size, "buff");
        castbar_   = MenuSlot(base, size, "casttime");
        targetWin_ = MenuSlot(base, size, "targetwi");
        if (s1 == 0 || s2 == 0)
        {
            ctx.Print("Native UI: party/target frame signatures not found (client version?); native frames stay visible.");
            return;
        }
        party0_ = ReadU32(s1 + 0x19);
        target_ = ReadU32(s1 + 0x23);
        party1_ = ReadU32(s2 + 0x01);
        party2_ = ReadU32(s2 + 0x07);
        found_  = true;
    }

    void NativeUiModule::Apply(bool restoreAll)
    {
        // win slots are re-read every frame (FancyChat does the same: the objects are recreated).
        if (!restoreAll && chatFound_ && hideChat_ != nullptr && *hideChat_)
        {
            PinChatClosed(ReadU32(chatWin1_));
            PinChatClosed(ReadU32(chatWin2_));
        }
        const bool showCompass = restoreAll || !hideCompass_;
        // Doors, counters and other fixed objects get no FarplaneUI ring / target info, so the game's own
        // target frame and cursor stay for them (unless "Show on objects" is on in Target).
        const bool showTarget  = restoreAll || !hideTarget_ || targetIsObject_;
        SetVisible(compass_, showCompass ? 1 : 0);
        PatchByte(compassByte_, showCompass ? 1 : 0);
        SetVisible(buffs_, (restoreAll || !hideBuffs_) ? 1 : 0);
        SetVisible(castbar_, (restoreAll || !hideCastBar_) ? 1 : 0);
        SetVisible(targetWin_, showTarget ? 1 : 0);
        ApplyLive(restoreAll);
        if (!found_)
            return;
        // The fish's fatigue gauge shares the party list's UI object (tested in game: only the party hide
        // blocks it), so the party hide is lifted while a fish is on the line (status 57).
        const uint8_t party = (restoreAll || !hideParty_ || fishing_) ? 1 : 0;
        const uint8_t tgt   = showTarget ? 1 : 0;
        SetVisible(party0_, party);
        SetVisible(party1_, party);
        SetVisible(party2_, party);
        SetVisible(target_, tgt);
    }

    namespace
    {
        // Live window list: node next +0, window +0x10, skip if byte +0x14; window header *(+4) with
        // "menu    <name8>" at +0x46, owner *(+0xC) pointing back at the window at +8; rect int16 at +0x42.
        template <typename F>
        void ForEachWindow(uintptr_t manager, F&& fn)
        {
            if (manager == 0)
                return;
            uintptr_t node = ReadU32(manager);
            for (int guard = 0; node != 0 && guard < 512; ++guard, node = ReadU32(node))
            {
                if (Ashita::Memory::SafeRead<uint8_t>(node + 0x14, 1) != 0)
                    continue;
                const uintptr_t win = ReadU32(node + 0x10);
                const uintptr_t hdr = ReadU32(win + 4), own = ReadU32(win + 0xC);
                if (win == 0 || hdr == 0 || own == 0 || ReadU32(own + 8) != win)
                    continue;
                char name[17]{};
                for (int i = 0; i < 16; ++i)
                    name[i] = static_cast<char>(Ashita::Memory::SafeRead<uint8_t>(hdr + 0x46 + static_cast<uintptr_t>(i), 0));
                fn(win, own, name);
            }
        }
    } // namespace

    // Hide extra live windows by name ("weather" matches "menu    weather "): the owner object takes the
    // same visibility bytes as the table windows (prim = *(owner + 8) = the window).
    void NativeUiModule::ApplyLive(bool restoreAll)
    {
        if (hideWins_.empty())
            return;
        ForEachWindow(manager_, [&](uintptr_t win, uintptr_t, const char* name) {
            std::string n(name + 8, 8);
            while (!n.empty() && n.back() == ' ')
                n.pop_back();
            for (const auto& h : hideWins_)
                if (_stricmp(n.c_str(), h.c_str()) == 0 && Writable(win + 0x69, 2))
                {
                    *reinterpret_cast<volatile uint8_t*>(win + 0x69) = restoreAll ? 1 : 0;
                    *reinterpret_cast<volatile uint8_t*>(win + 0x6A) = restoreAll ? 1 : 0;
                }
        });
    }
    void NativeUiModule::ListWindows(Context& ctx)
    {
        if (manager_ == 0)
        {
            ctx.Print("Native UI: window list not found.");
            return;
        }
        int n = 0;
        ForEachWindow(manager_, [&](uintptr_t win, uintptr_t, const char* name) {
            char b[160]{};
            ::sprintf_s(b, "  \"%.16s\"  rect %d,%d - %d,%d  shown %u/%u", name, Ashita::Memory::SafeRead<int16_t>(win + 0x42, 0),
                Ashita::Memory::SafeRead<int16_t>(win + 0x44, 0), Ashita::Memory::SafeRead<int16_t>(win + 0x46, 0),
                Ashita::Memory::SafeRead<int16_t>(win + 0x48, 0), Ashita::Memory::SafeRead<uint8_t>(win + 0x69, 0),
                Ashita::Memory::SafeRead<uint8_t>(win + 0x6A, 0));
            ctx.Print(b);
            ++n;
        });
        ctx.Print("Native UI: " + std::to_string(n) + " open game windows. Hide one with /fpui native hidewin <name> (e.g. weather).");
    }

    void NativeUiModule::Frame(Context& ctx)
    {
        if (!scanned_)
            Scan(ctx);
        // Fishing statuses: 38-43 (fish on the line, caught, rod / line break, monster, stop) and 50-62
        // (casting and the newer fishing animations).
        {
            auto* mm  = ctx.core->GetMemoryManager();
            auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
            auto* pty = mm != nullptr ? mm->GetParty() : nullptr;
            const uint32_t st = ent != nullptr && pty != nullptr ? ent->GetStatus(pty->GetMemberTargetIndex(0)) : 0;
            status_  = st;
            // Fish on the line: 57 (38 on older clients). The gauge only exists then; the other fishing
            // statuses linger after the catch, which kept the party list showing.
            fishing_ = st == 57 || st == 38;
            auto* tgt = mm != nullptr ? mm->GetTarget() : nullptr;
            const uint32_t ti = tgt != nullptr ? tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u) : 0;
            noTarget_       = ti == 0;
            targetIsObject_ = ti != 0 && ent != nullptr && ent->GetRawEntity(ti) != nullptr && !mod::ShowObjects() && mod::IsObject(ent, ti);
        }
        if (!enabled_)
        {
            if (wasEnabled_)
            {
                Apply(true); // put everything back once
                if (clockHidden_)
                    SetClock(false);
                wasEnabled_ = false;
            }
            return;
        }
        wasEnabled_ = true;
        Apply(false);
        // Clock: the game's own command, sent only when the wanted state changes (and once logged in).
        core_ = ctx.core;
        auto* party = ctx.core->GetMemoryManager() != nullptr ? ctx.core->GetMemoryManager()->GetParty() : nullptr;
        if (party != nullptr && party->GetMemberName(0) != nullptr && party->GetMemberName(0)[0] != '\0' && clockHidden_ != hideClock_)
            SetClock(hideClock_);
    }
    void NativeUiModule::SetClock(bool hidden)
    {
        if (core_ != nullptr && core_->GetChatManager() != nullptr)
            core_->GetChatManager()->QueueCommand(-1, hidden ? "/clock off" : "/clock on");
        clockHidden_ = hidden;
    }
    void NativeUiModule::Release(void)
    {
        Apply(true);
        if (clockHidden_)
            SetClock(false);
    }

    void NativeUiModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        hideParty_   = cfg->GetBool(alias, "native", "hideParty", hideParty_);
        hideTarget_  = cfg->GetBool(alias, "native", "hideTargetFrame", true); // target frame + "Target" window (FarplaneUI has its own)
        hideCompass_ = cfg->GetBool(alias, "native", "hideCompass", hideCompass_);
        hideClock_   = cfg->GetBool(alias, "native", "hideClock", hideClock_);
        hideBuffs_   = cfg->GetBool(alias, "native", "hideBuffs", hideBuffs_);
        hideCastBar_ = cfg->GetBool(alias, "native", "hideCastBar", hideCastBar_);
        hideWins_.clear();
        const char* list = cfg->GetString(alias, "native", "hideWindows");
        std::string cur;
        for (const char* p = list != nullptr ? list : ""; ; ++p)
        {
            if (*p == ',' || *p == '\0')
            {
                if (!cur.empty())
                    hideWins_.push_back(cur);
                cur.clear();
                if (*p == '\0')
                    break;
            }
            else if (*p != ' ')
                cur += *p;
        }
    }
    void NativeUiModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        ui::SetB(cfg, alias, "native", "hideParty", hideParty_);
        ui::SetB(cfg, alias, "native", "hideTargetFrame", hideTarget_);
        ui::SetB(cfg, alias, "native", "hideCompass", hideCompass_);
        ui::SetB(cfg, alias, "native", "hideClock", hideClock_);
        ui::SetB(cfg, alias, "native", "hideBuffs", hideBuffs_);
        ui::SetB(cfg, alias, "native", "hideCastBar", hideCastBar_);
        std::string list;
        for (const auto& h : hideWins_)
            list += (list.empty() ? "" : ",") + h;
        cfg->SetValue(alias, "native", "hideWindows", list.c_str());
    }

    auto NativeUiModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string what = args.empty() ? std::string() : ui::Lower(args[0]);
        const std::string v    = args.size() > 1 ? ui::Lower(args[1]) : std::string();
        auto set = [&](bool& flag) { flag = (v == "on") ? true : (v == "off") ? false : !flag; };
        if (what == "all")
        {
            enabled_ = v == "on" ? true : v == "off" ? false : !enabled_;
            ctx.Print(std::string("Native UI: hiding ") + (enabled_ ? "on." : "off - the game's own UI is left alone."));
            return true;
        }
        if (what == "party")
            set(hideParty_);
        else if (what == "target")
            set(hideTarget_);
        else if (what == "chat" && hideChat_ != nullptr)
            set(*hideChat_);
        else if (what == "compass")
            set(hideCompass_);
        else if (what == "clock")
            set(hideClock_);
        else if (what == "buffs" || what == "status")
            set(hideBuffs_);
        else if (what == "castbar")
            set(hideCastBar_);
        else if (what == "windows")
        {
            ListWindows(ctx);
            return true;
        }
        else if ((what == "hidewin" || what == "showwin") && args.size() > 1)
        {
            const std::string name = ui::Lower(args[1]);
            ApplyLive(true); // show everything first, then re-apply the new list
            std::erase_if(hideWins_, [&](const std::string& h) { return _stricmp(h.c_str(), name.c_str()) == 0; });
            if (what == "hidewin")
                hideWins_.push_back(name);
            ctx.MarkDirty();
            ctx.Print("Native UI: hidden windows: " + (hideWins_.empty() ? std::string("none") : [&] {
                std::string l;
                for (const auto& h : hideWins_)
                    l += (l.empty() ? "" : ", ") + h;
                return l;
            }()));
            return true;
        }
        else
        {
            {
                auto* mm  = ctx.core->GetMemoryManager();
                auto* tgt = mm->GetTarget();
                auto* ent = mm->GetEntity();
                const uint32_t ti = tgt != nullptr ? tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u) : 0;
                const char* tn    = ti != 0 && ent != nullptr && ent->GetRawEntity(ti) != nullptr ? ent->GetName(ti) : nullptr;
                char b[200]{};
                ::sprintf_s(b, "Native UI: player status %u%s; target index %u \"%s\" type %u spawn 0x%X; me %u", status_, fishing_ ? " (fishing)" : "", ti,
                    tn != nullptr ? tn : "", ti != 0 && ent != nullptr ? static_cast<unsigned>(ent->GetType(ti)) : 0u,
                    ti != 0 && ent != nullptr ? static_cast<unsigned>(ent->GetSpawnFlags(ti)) : 0u, mm->GetParty()->GetMemberTargetIndex(0));
                ctx.Print(b);
            }
            ctx.Print(std::string("Native UI hidden - party: ") + (hideParty_ ? "yes" : "no") + ", target: " + (hideTarget_ ? "yes" : "no") +
                      ", chat log: " + (hideChat_ != nullptr && *hideChat_ ? "yes" : "no") + (chatFound_ ? "" : " (chat window signature not found)") +
                      (found_ ? "" : " (frame signatures not found)") + ". Compass " + (compassByte_ != 0 ? "found" : "not found") + ", status icons " +
                      (buffs_ != 0 ? "found" : "not found") + ", cast bar " + (castbar_ != 0 ? "found" : "not found") + ", target window " +
                      (targetWin_ != 0 ? "found" : "not found") + ". Usage: /fpui native party|target|chat|compass|clock|buffs|castbar [on|off] | windows | hidewin|showwin <name>");
            return true;
        }
        Apply(false);
        ctx.MarkDirty();
        return Command(ctx, {});
    }

    void NativeUiModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Hide party list", "Hide the game's party / alliance list.", hideParty_);
        ch |= g.Toggle("Hide target frame", "Hide the game's target frame and \"Target\" window (FarplaneUI's target bar replaces them).", hideTarget_);
        if (hideChat_ != nullptr)
            ch |= g.Toggle("Hide chat log", "Hide the game's chat log window (its input line stays).", *hideChat_);
        ch |= g.Toggle("Hide compass", "Hide the game's compass (FarplaneUI has its own).", hideCompass_);
        ch |= g.Toggle("Hide clock", "Hide the game's clock with /clock off (restored with /clock on).", hideClock_);
        ch |= g.Toggle("Hide status icons", "Hide the game's status icons at the upper left.", hideBuffs_);
        ch |= g.Toggle("Hide cast bar", "Hide the game's cast bar (FarplaneUI has its own).", hideCastBar_);
        if (ch)
            ctx.MarkDirty();
        g.Info("Chat window", chatFound_ ? "Signature found." : "Signature NOT found - the native log can't be hidden.");
        g.Info("Frames", found_ ? "Signatures found." : "Signatures NOT found - party/target frames can't be hidden.");
        g.Info("Compass / icons / cast bar", (std::string(compassByte_ != 0 ? "compass" : "-") + ", " + (buffs_ != 0 ? "status icons" : "-") + ", " +
                                                  (castbar_ != 0 ? "cast bar" : "-") + ", " + (targetWin_ != 0 ? "target window" : "-") + " found").c_str());
        g.Info("On unload", "Everything is restored when FarplaneUI unloads.");
    }
} // namespace farplaneui
