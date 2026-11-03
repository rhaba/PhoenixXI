/**
 * Farplane UI - helpers shared by the built-in modules.
 */
#include "common.hpp"
#include "../ember.hpp"

namespace farplaneui::mod
{
    auto BeginPanel(Context& ctx, IGuiManager* gui, const char* id, bool* show, bool docked, const Rect& dock, const Rect& user, bool locked,
        float bgAlpha, bool useArt, ImGuiWindowFlags extra, bool border) -> Panel
    {
        Panel p{gui, false, 0, 0};
        const ImGuiWindowFlags f = ui::DockWindow(gui, docked, dock, user, locked) | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing | extra;
        if (ember::On())
        {
            // Inside the dock: no frame of its own (the dock is the frame). Elsewhere: a glass panel.
            const Rect& d     = ctx.layout.dock;
            const bool inDock = docked && d.w > 0 && dock.y >= d.y - 1 && dock.y + dock.h <= d.y + d.h + 1;
            // The glass is the window's own rounded background + border (an item the size of the window
            // would grow auto-resizing windows every frame).
            const auto& t    = ember::T();
            const bool glass = !inDock && bgAlpha > 0.0f;
            gui->PushStyleColor(ImGuiCol_WindowBg, ToVec4(glass ? t.glass : Rgba{0, 0, 0, 0}));
            gui->PushStyleColor(ImGuiCol_Border, ToVec4(glass ? t.edge : Rgba{0, 0, 0, 0}));
            gui->PushStyleColor(ImGuiCol_Text, ToVec4(t.ivory));
            p.colors = 3;
            gui->PushStyleVar(ImGuiStyleVar_WindowBorderSize, glass ? 1.0f : 0.0f);
            gui->PushStyleVar(ImGuiStyleVar_WindowRounding, glass ? t.S(t.radius) : 0.0f);
            gui->PushStyleVar(ImGuiStyleVar_WindowPadding, inDock ? ImVec2(0, 0) : ImVec2(12, 6));
            p.vars = 3;
            p.open = gui->Begin(id, show, f);
            return p;
        }
        UINT tw = 0, th = 0;
        IDirect3DTexture8* frame = useArt ? ctx.Art("panel_9slice", &tw, &th) : nullptr;
        gui->PushStyleColor(ImGuiCol_WindowBg, ToVec4(Rgba{art::twilight_dark.r, art::twilight_dark.g, art::twilight_dark.b, bgAlpha}));
        gui->PushStyleColor(ImGuiCol_Border, ToVec4(frame != nullptr ? Rgba{0, 0, 0, 0} : art::ember));
        gui->PushStyleColor(ImGuiCol_Text, ToVec4(art::amber));
        p.colors = 3;
        gui->PushStyleVar(ImGuiStyleVar_WindowBorderSize, frame != nullptr || !border ? 0.0f : 1.0f);
        gui->PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding, frame != nullptr ? ImVec2(14, 12) : ImVec2(8, 6));
        p.vars = 3;
        p.open = gui->Begin(id, show, f);
        if (p.open && frame != nullptr)
        {
            ui::NineSlice(gui, frame, tw, th, gui->GetWindowPos(), gui->GetWindowSize(), 40.0f, 16.0f, Rgba{1, 1, 1, 1}, 1.0f);
        }
        return p;
    }

    void StripBar(Context& ctx, IGuiManager* gui, float frac, const Rgba& fill)
    {
        const ImVec2 bp = gui->GetCursorScreenPos();
        const float w   = gui->GetContentRegionAvail().x;
        DrawBar(ctx, gui, bp, ImVec2(w, kStripBarH), frac, fill, 1.0f, true);
        gui->Dummy(ImVec2(w, kStripBarH));
    }

    namespace
    {
        // The game's UTC clock: FFXiMain global -> object -> +0x0C.
        auto UtcStamp(void) -> uint32_t
        {
            static uintptr_t pointer = 0;
            static bool scanned      = false;
            if (!scanned)
            {
                scanned = true;
                if (const HMODULE m = ::GetModuleHandleA("FFXiMain.dll"); m != nullptr)
                {
                    const auto base = reinterpret_cast<uintptr_t>(m);
                    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                    const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<uintptr_t>(dos->e_lfanew));
                    pointer = Ashita::Memory::FindPattern(base, nt->OptionalHeader.SizeOfImage, "8B0D????????8B410C8B49108D04808D04808D04808D04C1C3", 2, 0);
                }
            }
            if (pointer == 0)
                return 0;
            const auto global = *reinterpret_cast<const uint32_t*>(pointer);
            const auto object = global != 0 ? *reinterpret_cast<const uint32_t*>(global) : 0;
            return object != 0 ? *reinterpret_cast<const uint32_t*>(object + 0x0C) : 0;
        }
    } // namespace

    auto IsObject(IEntity* ent, uint32_t index) -> bool
    {
        if (ent == nullptr || (ent->GetSpawnFlags(index) & 0x11u) != 0) // 0x01 player, 0x10 monster
            return false;
        return ent->GetType(index) >= 2;
    }
    auto ShowObjects(void) -> bool&
    {
        static bool show = false;
        return show;
    }

    auto EarthTime(void) -> uint32_t
    {
        const uint32_t utc = UtcStamp();
        return utc != 0 ? utc : static_cast<uint32_t>(::time(nullptr));
    }

    auto NearbyMobs(Context& ctx) -> const std::vector<uint16_t>&
    {
        static std::vector<uint16_t> mobs;
        static ULONGLONG scannedAt = 0;
        const ULONGLONG now = ::GetTickCount64();
        if (now - scannedAt < 200)
            return mobs;
        scannedAt = now;
        mobs.clear();
        auto* mm  = ctx.core->GetMemoryManager();
        auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
        if (ent == nullptr)
            return mobs;
        for (uint32_t i = 1; i < 0x400; ++i)
            if (ent->GetRawEntity(i) != nullptr && (ent->GetSpawnFlags(i) & 0x10u) != 0)
                mobs.push_back(static_cast<uint16_t>(i));
        return mobs;
    }

    auto PlayerBuffs(Context& ctx) -> std::vector<Buff>
    {
        std::vector<Buff> out;
        auto* mm     = ctx.core->GetMemoryManager();
        auto* player = mm != nullptr ? mm->GetPlayer() : nullptr;
        if (player == nullptr)
            return out;
        const int16_t* icons   = player->GetStatusIcons();
        const uint32_t* timers = player->GetStatusTimers();
        const uint32_t utc     = UtcStamp();
        const uint32_t clock   = utc != 0 ? (utc - 0x3C307D70u) * 60u : 0;
        for (uint32_t i = 0; icons != nullptr && i < 32; ++i)
        {
            const int16_t sid = icons[i];
            if (sid <= 0 || sid == 255)
                continue;
            Buff b{static_cast<uint16_t>(sid), i, -1.0};
            if (timers != nullptr && utc != 0 && timers[i] != 0x7FFFFFFFu)
            {
                const auto left = static_cast<int32_t>(timers[i] - clock); // wraps correctly
                if (left <= 0)
                    continue;
                b.seconds = left / 60.0;
            }
            out.push_back(b);
        }
        return out;
    }
    namespace
    {
        auto TopMenuHeaderAt(uintptr_t base) -> uintptr_t
        {
            const uintptr_t sub   = Ashita::Memory::SafeRead<uint32_t>(base, 0u);
            const uintptr_t value = sub != 0 ? Ashita::Memory::SafeRead<uint32_t>(sub, 0u) : 0u;
            return value != 0 ? Ashita::Memory::SafeRead<uint32_t>(value + 4, 0u) : 0u;
        }
        uintptr_t g_menuBase = 0;
    } // namespace
    auto TopMenuObject(void) -> uintptr_t
    {
        (void)TopMenuName();
        if (g_menuBase == 0)
            return 0;
        const uintptr_t sub = Ashita::Memory::SafeRead<uint32_t>(g_menuBase, 0u);
        return sub != 0 ? Ashita::Memory::SafeRead<uint32_t>(sub, 0u) : 0u;
    }
    auto TopMenuHeader(void) -> uintptr_t
    {
        (void)TopMenuName(); // ensures the scan ran
        return g_menuBase != 0 ? TopMenuHeaderAt(g_menuBase) : 0;
    }
    auto TopMenuName(void) -> std::string
    {
        static uintptr_t base = 0;
        static bool scanned   = false;
        if (!scanned)
        {
            scanned = true;
            if (const HMODULE m = ::GetModuleHandleA("FFXiMain.dll"); m != nullptr)
            {
                const auto b    = reinterpret_cast<uintptr_t>(m);
                const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
                const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(b + static_cast<uintptr_t>(dos->e_lfanew));
                base = Ashita::Memory::FindPattern(b, nt->OptionalHeader.SizeOfImage, "8B480C85C974??8B510885D274??3B05", 16, 0);
                g_menuBase = base;
            }
        }
        if (base == 0)
            return {};
        const uintptr_t header = TopMenuHeaderAt(base);
        if (header == 0)
            return {};
        char name[17]{};
        for (int i = 0; i < 16; ++i)
            name[i] = static_cast<char>(Ashita::Memory::SafeRead<uint8_t>(header + 0x46 + static_cast<uintptr_t>(i), static_cast<uint8_t>(0)));
        std::string out(name);
        while (!out.empty() && (out.back() == ' '))
            out.pop_back();
        return out;
    }

    auto BuffName(Context& ctx, uint16_t id) -> std::string
    {
        const char* nm = ctx.core->GetResourceManager()->GetString("buffs.names", id);
        return nm != nullptr && nm[0] != '\0' ? std::string(nm) : "Status " + std::to_string(id);
    }
    auto StatusIcon(Context& ctx, uint16_t id) -> IDirect3DTexture8*
    {
        IStatusIcon* si = ctx.core->GetResourceManager()->GetStatusIconByIndex(id);
        if (si == nullptr || si->ImageSize == 0 || si->ImageSize > sizeof(si->Bitmap))
            return nullptr;
        return ctx.textures->Memory(0x10000u | id, si->Bitmap, si->ImageSize);
    }

    void EndPanel(Panel& p, Rect* user, bool docked, Context& ctx)
    {
        if (p.open && user != nullptr && !docked)
        {
            if (ui::ReadBack(p.gui, *user))
                ctx.MarkDirty();
        }
        p.gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        p.gui->End();
        p.gui->PopStyleVar(p.vars);
        p.gui->PopStyleColor(p.colors);
    }

    auto DockCommand(Context& ctx, const char* name, const std::vector<std::string>& args, bool& show, bool& docked, bool& locked) -> bool
    {
        const std::string sub = args.empty() ? std::string("toggle") : ui::Lower(args[0]);
        const std::string n(name);
        if (sub == "toggle" || sub == "show" || sub == "hide" || sub == "on" || sub == "off")
        {
            show = (sub == "toggle") ? !show : (sub == "show" || sub == "on");
            ctx.Print(n + (show ? " shown." : " hidden."));
        }
        else if (sub == "dock")
        {
            docked = true;
            ctx.Print(n + " docked into the UI mask.");
        }
        else if (sub == "undock" || sub == "free")
        {
            docked = false;
            ctx.Print(n + " undocked: drag/resize it, then /fpui " + n + " lock.");
        }
        else if (sub == "lock" || sub == "unlock")
        {
            locked = (sub == "lock");
            ctx.Print(n + (locked ? " locked." : " unlocked."));
        }
        else
        {
            return false;
        }
        ctx.MarkDirty();
        return true;
    }

    void DockSettings(cfg::Grid& g, Context& ctx, bool& show, bool& docked, bool& locked)
    {
        bool ch = g.Toggle("Enabled", "Show this panel.", show);
        ch |= g.Toggle("Docked", "Positioned and sized by the UI layout. Off: drag and resize freely (saved).", docked);
        if (!docked)
            ch |= g.Toggle("Locked", "Stop the panel being moved or resized.", locked);
        if (ch)
            ctx.MarkDirty();
    }

    void LoadDock(IConfigurationManager* cfg, const char* alias, const char* sec, bool& show, bool& docked, bool& locked, Rect& user)
    {
        show   = cfg->GetBool(alias, sec, "show", show);
        docked = cfg->GetBool(alias, sec, "docked", docked);
        locked = cfg->GetBool(alias, sec, "locked", locked);
        ui::LoadRect(cfg, alias, sec, "win", user);
    }
    void SaveDock(IConfigurationManager* cfg, const char* alias, const char* sec, bool show, bool docked, bool locked, const Rect& user)
    {
        ui::SetB(cfg, alias, sec, "show", show);
        ui::SetB(cfg, alias, sec, "docked", docked);
        ui::SetB(cfg, alias, sec, "locked", locked);
        ui::SaveRect(cfg, alias, sec, "win", user);
    }
} // namespace farplaneui::mod
