/**
 * Farplane UI - Copyright (c) 2026
 *
 * Holistic refined/ornate UI replacement for Final Fantasy XI, as one Ashita v4 C++ plugin.
 * v0.3: module host. Each feature is an IModule (module.hpp); see README "Adding a module".
 */

#include "farplaneui.hpp"
#include "cfgui.hpp"
#include "pad.hpp"
#include "lsgate.hpp"
#include "ember.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kAlias = "farplaneui";

        auto Tokens(const char* command) -> std::vector<std::string>
        {
            std::vector<std::string> out;
            std::istringstream in(command != nullptr ? command : "");
            std::string tok;
            while (in >> tok)
                out.push_back(tok);
            return out;
        }
        auto NowSeconds(void) -> double
        {
            return static_cast<double>(::GetTickCount64()) / 1000.0;
        }
        void HostPrint(void* host, const std::string& text) { static_cast<plugin*>(host)->Print(text); }
        void HostDirty(void* host) { static_cast<plugin*>(host)->MarkDirty(); }
    } // namespace

    plugin::plugin(void)
        : core_{nullptr}
        , log_{nullptr}
        , device_{nullptr}
        , id_{0}
    {
        auto mask = std::make_unique<MaskModule>();
        auto chat = std::make_unique<ChatModule>();
        mask_     = mask.get();
        chat_     = chat.get();
        modules_.push_back(std::move(mask));
        modules_.push_back(std::move(chat));
        modules_.push_back(std::make_unique<MinimapModule>());
        modules_.push_back(std::make_unique<InventoryModule>());
        modules_.push_back(std::make_unique<PartyModule>());
        modules_.push_back(std::make_unique<EquipModule>());
        modules_.push_back(std::make_unique<TargetModule>());
        modules_.push_back(std::make_unique<TargetBarModule>());
        modules_.push_back(std::make_unique<PointsModule>());
        modules_.push_back(std::make_unique<TicksModule>());
        modules_.push_back(std::make_unique<NotifyModule>());
        modules_.push_back(std::make_unique<RecastModule>());
        modules_.push_back(std::make_unique<CastBarModule>());
        modules_.push_back(std::make_unique<StatusModule>());
        modules_.push_back(std::make_unique<EnemyModule>());
        modules_.push_back(std::make_unique<ReadyCheckModule>());
        modules_.push_back(std::make_unique<AllianceModule>());
        modules_.push_back(std::make_unique<CompassModule>());
        modules_.push_back(std::make_unique<CommandMenuModule>());
        modules_.push_back(std::make_unique<BarStyleModule>());
        modules_.push_back(std::make_unique<DpsModule>());
        modules_.push_back(std::make_unique<QuestModule>());
        modules_.push_back(std::make_unique<ShadowModule>());
        modules_.push_back(std::make_unique<PadModule>());
        modules_.push_back(std::make_unique<NativeUiModule>(chat_->HideNativeFlag()));
    }

    plugin::~plugin(void) {}

    auto plugin::GetName(void) const -> const char* { return "FarplaneUI"; }
    auto plugin::GetAuthor(void) const -> const char* { return "Farplane Team"; }
    auto plugin::GetDescription(void) const -> const char* { return "A holistic refined/ornate UI replacement for Final Fantasy XI."; }
    auto plugin::GetLink(void) const -> const char* { return ""; }
    auto plugin::GetVersion(void) const -> double { return 0.113; }
    auto plugin::GetInterfaceVersion(void) const -> double { return ASHITA_INTERFACE_VERSION; }
    auto plugin::GetPriority(void) const -> int32_t { return 0; }
    auto plugin::GetFlags(void) const -> uint32_t
    {
        return (uint32_t)(Ashita::PluginFlags::UseDirect3D | Ashita::PluginFlags::UseCommands | Ashita::PluginFlags::UseText | Ashita::PluginFlags::UsePackets);
    }

    auto plugin::Initialize(IAshitaCore* core, ILogManager* logger, const uint32_t id) -> bool
    {
        core_ = core;
        log_  = logger;
        id_   = id;

        ctx_.core        = core;
        ctx_.textures    = &textures_;
        ctx_.resourceDir = std::string(core->GetInstallPath()) + "\\resources\\farplaneui\\";
        ctx_.alias       = kAlias;
        ctx_.printFn     = &HostPrint;
        ctx_.commandFn   = [](void* h, const char* t) -> bool { return static_cast<plugin*>(h)->HandleCommand(1, t, false); };
        ctx_.dirtyFn     = &HostDirty;
        ctx_.host        = this;

        LoadAll();
        for (auto& m : modules_)
            m->Initialize(ctx_);
        return true;
    }

    auto plugin::Release(void) -> void
    {
        if (perf_.on)
            PerfFinish("plugin unloaded");
        if (dirty_ && core_ != nullptr)
            SaveAll();
        dirty_ = false;
        for (auto& m : modules_)
            m->Release();
        worldRing_.Uninstall();
        textures_.Release();
    }

    void plugin::LoadAll(void)
    {
        mask_->settings.Load(core_); // creates/loads the ini file, reads [settings]
        const int t = FindTheme(mask_->settings.theme.c_str());
        mask_->theme  = t >= 0 ? t : kDefaultTheme;
        ctx_.palette  = &kThemes[mask_->theme];
        auto* cfg     = core_->GetConfigurationManager();
        if (cfg == nullptr)
            return;
        chatWidth_ = std::clamp(cfg->GetFloat(kAlias, "layout", "chatWidth", chatWidth_), 0.15f, 0.8f);
        ember::On() = cfg->GetBool(kAlias, "layout", "emberDock", true);
        ember::T().dockH = std::clamp(cfg->GetFloat(kAlias, "layout", "dockSize", ember::T().dockH), 140.0f, 600.0f);
        for (auto& m : modules_)
            m->Load(cfg, kAlias);
    }

    void plugin::SaveAll(void)
    {
        auto* cfg = core_->GetConfigurationManager();
        if (cfg == nullptr)
            return;
        ui::SetF(cfg, kAlias, "layout", "chatWidth", chatWidth_);
        ui::SetB(cfg, kAlias, "layout", "emberDock", ember::On());
        ui::SetF(cfg, kAlias, "layout", "dockSize", ember::T().dockH);
        for (auto& m : modules_)
            m->Save(cfg, kAlias);
        mask_->settings.Save(core_); // writes [settings] and saves the file
    }

    void plugin::MarkDirty(void)
    {
        mask_->settings.Clamp();
        ctx_.palette = &kThemes[mask_->theme];
        dirty_       = true;
        dirty_at_    = NowSeconds();
    }

    void plugin::Print(const std::string& text) const
    {
        if (core_ == nullptr || core_->GetChatManager() == nullptr)
            return;
        const std::string line = Ashita::Chat::Header("farplaneui") + Ashita::Chat::Message(text);
        core_->GetChatManager()->Write(1, false, line.c_str());
    }

    auto plugin::InCutscene(void) const -> bool
    {
        if (core_ == nullptr)
            return false;
        auto* mm = core_->GetMemoryManager();
        if (mm == nullptr || mm->GetParty() == nullptr || mm->GetEntity() == nullptr)
            return false;
        const uint32_t idx = mm->GetParty()->GetMemberTargetIndex(0);
        if (idx == 0 || idx >= 2304)
            return false;
        return mm->GetEntity()->GetStatus(idx) == 4;
    }

    // Layout inside the mask band, left to right:
    //   [chat][minimap][bags] ...(buffs, later)... [party][equip]
    void plugin::ComputeLayout(void)
    {
        Layout& L  = ctx_.layout;
        L.screenW  = static_cast<float>(mask_->mask.ScreenW());
        L.screenH  = static_cast<float>(mask_->mask.ScreenH());
        mask_->Band(L.bandTop, L.bandBottom);
        L.bandVisible = mask_->settings.enabled;
        L.dock = Rect{};
        if (ember::On())
        {
            ComputeEmberLayout();
            return;
        }
        const float m = 8.0f;
        const float top = L.bandTop + m, h = std::max(40.0f, L.bandBottom - L.bandTop - 2 * m);
        L.chat      = Rect{m, top, std::floor(L.screenW * chatWidth_), h};
        // The minimap's frame sits flush against the chat and spans the chat's full height.
        L.minimap   = Rect{L.chat.x + L.chat.w + 2.0f, top, h, h};
        // Column to its right: the DPS box, with the one-line inventory box along the bottom.
        const float colW = 330.0f, invH = 30.0f;
        L.dps       = Rect{L.minimap.x + L.minimap.w + m, top, colW, h - invH - m * 0.5f};
        L.inventory = Rect{L.dps.x, top + h - invH, colW, invH};
        const float eq = std::min(h, 4 * 44.0f + 28.0f);
        L.equip     = Rect{L.screenW - m - eq, top, eq, h};
        L.party     = Rect{L.equip.x - m - 300.0f, top, 300.0f, h};
        // Two equal panels sitting on the band's top rail: a text row over a thin bar each.
        const float stripH = 46.0f, stripY = L.bandTop - stripH - 6.0f;
        // Centred: target bar | resting ticks | points, the ticks panel straddling the screen centre.
        const float ticksW = 260.0f;
        const float stripW = std::min((L.screenW - ticksW) * 0.5f - 3 * m, std::max(320.0f, (L.minimap.x + L.minimap.w - L.chat.x) * 0.5f));
        const float cx     = L.screenW * 0.5f;
        L.ticks     = Rect{cx - ticksW * 0.5f, stripY, ticksW, stripH};
        L.targetbar = Rect{L.ticks.x - m - stripW, stripY, stripW, stripH};
        L.points    = Rect{L.ticks.x + ticksW + m, stripY, stripW, stripH};
    }

    // Ember Dock (incoming/ember_dock/WIRING.md section 2): one glass dock along the bottom,
    // chat | minimap | DPS (bag gauges under it) | party | equipment; the strip panels sit above it.
    void plugin::ComputeEmberLayout(void)
    {
        Layout& L = ctx_.layout;
        auto& t   = ember::T();
        t.scale   = L.screenH / 1080.0f;
        const float M = t.S(t.margin), P = t.S(t.padding), G = t.S(t.gap);
        const float dockY = L.screenH - t.S(t.dockH);
        L.dock = Rect{M, dockY, L.screenW - 2 * M, L.screenH - dockY - M};
        const float top = L.dock.y + P, ih = L.dock.h - 2 * P;
        // Equipment: four square cells plus the gil row fill the dock's height (any resolution, aspect or dock
        // height); the width follows. Never smaller than the theme's cell, never past 2x it.
        const float cellGap = t.S(t.cellGap);
        const float eqCell  = std::clamp((ih - (t.S(t.gilH) + 2.0f) - 3 * cellGap) / 4.0f, t.S(t.cell), t.S(t.cell) * 2.0f);
        const float eqW     = 4 * eqCell + 3 * cellGap;
        const float fixed = ih + t.S(t.dpsW) + t.S(t.partyW) + eqW + 3 * G + t.S(t.laneGapLeft) + t.S(t.laneGapRight) + 2 * P + 2 * M + t.S(200); // 200: minimum buff lane
        const float chatW = std::max(t.S(420), std::min(std::floor(L.screenW * chatWidth_), L.screenW - fixed));
        L.chat      = Rect{L.dock.x + P, top, chatW, ih};
        L.minimap   = Rect{L.chat.x + L.chat.w + G, top, ih, ih};
        L.dps       = Rect{L.minimap.x + L.minimap.w + G, top, t.S(t.dpsW), ih};
        const float invBlock = 2 * t.S(t.invH) + t.S(t.invStackGap); // two stacked gauges
        L.inventory = Rect{L.dps.x, top + ih - invBlock, L.dps.w, invBlock};
        L.equip     = Rect{L.dock.x + L.dock.w - P - eqW, top, eqW, ih};
        L.party     = Rect{L.equip.x - G - t.S(t.partyW), top, t.S(t.partyW), ih};
        L.buffLane  = Rect{L.dps.x + L.dps.w + t.S(t.laneGapLeft), top, 0, ih};
        L.buffLane.w = std::max(0.0f, L.party.x - t.S(t.laneGapRight) - L.buffLane.x);
        // Strip panels (target bar | ticks | points) in glass above the dock, centred.
        const float m = 8.0f, stripH = 46.0f, stripY = L.dock.y - t.S(t.stripGap) - stripH;
        const float ticksW = 260.0f;
        const float stripW = std::min((L.screenW - ticksW) * 0.5f - 3 * m, std::max(320.0f, (L.minimap.x + L.minimap.w - L.chat.x) * 0.5f));
        const float cx     = L.screenW * 0.5f;
        L.ticks     = Rect{cx - ticksW * 0.5f, stripY, ticksW, stripH};
        L.targetbar = Rect{L.ticks.x - m - stripW, stripY, stripW, stripH};
        L.points    = Rect{L.ticks.x + ticksW + m, stripY, stripW, stripH};
    }

    auto plugin::HandleEvent(const char*, const void*, const uint32_t) -> void {}

    auto plugin::HandleCommand(int32_t mode, const char* command, bool injected) -> bool
    {
        UNREFERENCED_PARAMETER(mode);
        UNREFERENCED_PARAMETER(injected);
        auto args = Tokens(command);
        if (args.empty())
            return false;
        const std::string cmd = ui::Lower(args[0]);
        if (cmd == "/readycheck") // same command XIUI uses
        {
            args.erase(args.begin());
            for (auto& m : modules_)
                if (std::string(m->Name()) == "readycheck")
                    return m->Command(ctx_, args);
            return false;
        }
        if (cmd != "/farplane" && cmd != "/fpui" && cmd != "/farplaneui")
            return false;
        args.erase(args.begin());
        if (args.empty())
        {
            show_settings_window_ = !show_settings_window_;
            return true;
        }
        const std::string sub = ui::Lower(args[0]);
        if (sub == "lscheck")
        {
            const auto shells = lsgate::Shells(core_);
            if (shells.empty())
                Print("FarplaneUI: no linkshell items in your inventory.");
            for (const auto& sh : shells)
                Print("FarplaneUI: linkshell \"" + sh.name + "\" status " + std::to_string(sh.flags) + (sh.flags == 19 ? " (equipped)" : ""));
            Print(std::string("FarplaneUI: UI ") + (active_ ? "enabled." : "dormant (needs the Farplane linkshell equipped)."));
            return true;
        }
        if (!active_)
        {
            Print("FarplaneUI is dormant: equip the Farplane linkshell to enable it. (/fpui lscheck)");
            return true;
        }
        if (sub == "theme" || sub == "ember")
        {
            float h = 0;
            if (args.size() > 2 && ui::Lower(args[1]) == "height" && ui::ParseFloat(args[2], h))
            {
                ember::T().dockH = std::clamp(h, 140.0f, 600.0f);
                MarkDirty();
                Print("FarplaneUI: dock height " + std::to_string(static_cast<int>(ember::T().dockH)) + " (at 1080p).");
                return true;
            }
            const std::string a = args.size() > 1 ? ui::Lower(args[args.size() - 1]) : std::string();
            ember::On() = a == "on" ? true : a == "off" ? false : !ember::On();
            MarkDirty();
            Print(std::string("FarplaneUI: Ember Dock ") + (ember::On() ? "on." : "off (classic frames)."));
            return true;
        }
        if (sub == "perf")
        {
            const std::string arg = args.size() > 1 ? ui::Lower(args[1]) : std::string();
            if (arg == "stop" || arg == "off")
            {
                if (perf_.on)
                    PerfFinish("stopped");
                else
                    Print("FarplaneUI perf: not running.");
                return true;
            }
            if (perf_.on)
            {
                Print("FarplaneUI perf: already logging to " + perf_.file + " (/fpui perf stop)");
                return true;
            }
            PerfStart(arg.empty() ? 5.0 : std::clamp(std::atof(arg.c_str()), 0.1, 60.0));
            return true;
        }
        if (sub == "status")
        {
            char b[256]{};
            ::sprintf_s(b, "FarplaneUI v0.11.3 build %s %s | camera valid=%d captures=%u rejected=%u", __DATE__, __TIME__,
                ctx_.camera.valid ? 1 : 0, ctx_.camera.captures, ctx_.camera.rejected);
            Print(b);
            ReportStatus();
            return true;
        }
        if (sub == "save")
        {
            SaveAll();
            dirty_ = false;
            Print("Settings saved.");
            return true;
        }
        if (sub == "chatwidth" && args.size() > 1)
        {
            float f = 0;
            if (ui::ParseFloat(args[1], f))
            {
                chatWidth_ = std::clamp(f > 1.0f ? f / 100.0f : f, 0.15f, 0.8f);
                MarkDirty();
                Print("Chat width: " + std::to_string(static_cast<int>(chatWidth_ * 100)) + "% of the screen.");
                return true;
            }
        }
        if (sub == "fxdemo")
            for (auto& m : modules_)
                if (std::string(m->Name()) == "party")
                    return m->Command(ctx_, {"fxdemo"});
        // /fpui <module> ...
        for (auto& m : modules_)
        {
            if (sub == m->Name() && m.get() != mask_)
            {
                std::vector<std::string> rest(args.begin() + 1, args.end());
                if (!m->Command(ctx_, rest))
                    Print(std::string("Unknown ") + m->Name() + " command.");
                return true;
            }
        }
        // Top-level mask commands (Lua-compatible): on/off/lines/bottom/top/layer/setup/theme
        if (mask_->Command(ctx_, args))
            return true;
        Print("Usage: /fpui (settings) | status | save | chatwidth <%> | on|off | lines <n> | bottom <px|auto> | top <px> | "
              "layer [under|over] | setup | theme <name> | chat|party|minimap|inventory|equip|target|native ...");
        return true;
    }

    auto plugin::HandleIncomingText(int32_t mode, bool, const char* message, int32_t*, bool*, char* modifiedMessage, bool injected, bool blocked) -> bool
    {
        if (!active_)
            return false;
        // Another plugin/addon may already have rewritten the line; show what the game would show.
        const char* text = (modifiedMessage != nullptr && modifiedMessage[0] != '\0') ? modifiedMessage : message;
        bool block = false;
        for (size_t i = 0; i < modules_.size(); ++i)
        {
            // A line someone else blocked only goes to read-only modules (the ready check), never to the chat.
            if (blocked && !modules_[i]->ReadsBlockedText())
                continue;
            const bool timed = perf_.on;
            const double t0  = timed ? Qpc() : 0;
            block |= modules_[i]->IncomingText(ctx_, mode, text, injected);
            if (timed && perf_.on)
                perf_.text[i] += Qpc() - t0;
        }
        return block;
    }
    auto plugin::HandleOutgoingText(int32_t, const char*, int32_t*, char*, bool, bool) -> bool { return false; }
    auto plugin::HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t*, uint32_t, const uint8_t*, bool, bool) -> bool
    {
        if (!active_)
            return false;
        for (size_t i = 0; i < modules_.size(); ++i)
        {
            const bool timed = perf_.on;
            const double t0  = timed ? Qpc() : 0;
            modules_[i]->IncomingPacket(ctx_, id, size, data);
            if (timed && perf_.on)
                perf_.packet[i] += Qpc() - t0;
        }
        return false;
    }
    auto plugin::HandleOutgoingPacket(uint16_t, uint32_t, const uint8_t*, uint8_t*, uint32_t, const uint8_t*, bool, bool) -> bool { return false; }

    auto plugin::Direct3DInitialize(IDirect3DDevice8* device) -> bool
    {
        device_     = device;
        ctx_.device = device;
        textures_.Initialize(device);
        for (auto& m : modules_)
            m->Direct3DInit(ctx_);
        // Target ring inside the 3D scene; the target module falls back to a 2D ring without it.
        worldRing_.Install(device, &ctx_);
        mask_->mask.beforeDrawUser_ = &worldRing_;
        mask_->mask.beforeDraw_     = [](void* ring) { static_cast<WorldRingRenderer*>(ring)->DrawFront(); };
        d3dInit_ = true;
        return true;
    }

    auto plugin::Direct3DBeginScene(bool) -> void {}

    auto plugin::Direct3DEndScene(bool isRenderingBackBuffer) -> void
    {
        if (!active_)
            return;
        mask_->mask.OnEndScene(isRenderingBackBuffer, mask_->settings, *ctx_.palette, mask_->hidden);
    }

    auto plugin::Direct3DPresent(const RECT*, const RECT*, HWND, const RGNDATA*) -> void
    {
        if (core_ == nullptr)
            return;
        // Linkshell gate: dormant (nothing drawn, nothing hidden, no hooks acting) until the Farplane
        // linkshell is equipped; rechecked every 2 seconds once the inventory has loaded.
        if (!active_)
        {
            const double now = NowSeconds();
            if (now - gateCheckedAt_ < 2.0)
                return;
            gateCheckedAt_ = now;
            auto* party = core_->GetMemoryManager() != nullptr ? core_->GetMemoryManager()->GetParty() : nullptr;
            const bool loggedIn = party != nullptr && party->GetMemberName(0) != nullptr && party->GetMemberName(0)[0] != '\0' &&
                                  core_->GetMemoryManager()->GetInventory()->GetContainerCountMax(0) > 0;
            if (!loggedIn)
                return;
            if (lsgate::Member(core_))
            {
                active_ = true;
                Print("FarplaneUI: Farplane linkshell found - UI enabled.");
            }
            else
            {
                if (!gateNotified_)
                    Print("FarplaneUI is for the Farplane linkshell: equip it as your linkshell to enable the UI. (/fpui lscheck)");
                gateNotified_ = true;
                return;
            }
        }
        if (!reported_)
        {
            reported_ = true;
            ReportStatus();
        }
        mask_->mask.NewFrame();
        if (device_ != nullptr)
        {
            IDirect3DSurface8* bb = nullptr;
            if (SUCCEEDED(device_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb != nullptr)
            {
                ctx_.backBuffer = bb; // compared only
                bb->Release();
            }
            ctx_.targetIsBackBuffer = true; // Present: drawing restarts on the back buffer
        }
        // Fallback: if no draw call this frame had a usable perspective camera, use the device's
        // current transforms (what world-drawing addons such as boussole rely on at present time).
        // (v0.3.4: no Present-time fallback - device state at Present belongs to UI passes.)
        cameraThisFrame_ = false;
        ++ctx_.camera.frame;
        if (ctx_.camera.valid && ctx_.camera.frame - ctx_.camera.lastFrame > 30)
            ctx_.camera.valid = false; // stale
        mask_->hidden = mask_->settings.hideInCutscenes && InCutscene();
        if (dirty_ && NowSeconds() - dirty_at_ > 0.75)
        {
            SaveAll();
            dirty_ = false;
        }
        ComputeLayout();
        if (ember::On())
            ember::BuildDock(ctx_, mask_->mask.overlay_);
        else
            mask_->mask.overlay_.clear();
        ctx_.ring.want    = false; // the target module re-requests the 3D ring every frame
        ctx_.shadows.want = false; // and the shadows module its shadows
        ctx_.cast.want    = false; // and the cast bar its ring
        const double presentStart = perf_.on ? Qpc() : 0;
        for (size_t i = 0; i < modules_.size(); ++i)
        {
            const bool timed = perf_.on;
            const double t0  = timed ? Qpc() : 0;
            modules_[i]->Frame(ctx_);
            if (timed && perf_.on)
            {
                const double dt = Qpc() - t0;
                perf_.frame[i] += dt, perf_.cur[i] += dt;
            }
        }
        struct PerfEnd // count the frame however this function returns
        {
            plugin* self;
            double start;
            ~PerfEnd()
            {
                auto& p = self->perf_;
                if (!p.on || start == 0)
                    return; // not running, or started during this frame
                const double now = Qpc(), ours = now - start;
                p.present += ours, p.presentMax = std::max(p.presentMax, ours), p.frames += 1;
                if (p.lastPresent > 0)
                {
                    const double gap = now - p.lastPresent; // time between our frames = the game's frame time
                    p.gapMax = std::max(p.gapMax, gap);
                    if (gap > 0.050)
                        ++p.hitches;
                }
                p.lastPresent = now;
                for (size_t i = 0; i < p.cur.size(); ++i)
                    p.peak[i] = std::max(p.peak[i], p.cur[i]), p.cur[i] = 0;
                if (now - p.startedAt >= 5.0)
                    self->PerfWindow();
            }
        } perfEnd{this, presentStart};

        auto* gui = core_->GetGuiManager();
        if (gui == nullptr || mask_->hidden)
        {
            if (gui != nullptr)
                RenderSettingsWindow(gui);
            return;
        }
        {
            ScopedTheme theme(gui, *ctx_.palette);
            for (size_t i = 0; i < modules_.size(); ++i)
            {
                const bool timed = perf_.on;
                const double t0  = timed ? Qpc() : 0;
                modules_[i]->Render(ctx_, gui);
                if (timed && perf_.on)
                {
                    const double dt = Qpc() - t0;
                    perf_.render[i] += dt, perf_.cur[i] += dt;
                }
            }
        }
        RenderSettingsWindow(gui);
    }

    auto plugin::Direct3DSetRenderState(D3DRENDERSTATETYPE, DWORD*) -> bool { return false; }

    void plugin::OnDraw(void)
    {
        if (worldRing_.Drawing() || !active_)
            return; // our own ring draw, or dormant
        const double t0 = perf_.on ? Qpc() : 0;
        struct DrawTime
        {
            Perf& p;
            double t0;
            ~DrawTime()
            {
                if (p.on && t0 != 0)
                    p.draw += Qpc() - t0, p.drawCalls += 1;
            }
        } drawTime{perf_, t0};
        mask_->mask.OnDrawCall(mask_->settings, *ctx_.palette, mask_->hidden,
            ctx_.targetKnown ? (ctx_.targetIsBackBuffer ? 1 : 0) : -1);
        const double tMask = perf_.on && t0 != 0 ? Qpc() : 0;
        if (tMask != 0)
            perf_.drawMask += tMask - t0;
        // Capture the world camera once per frame: the first draw whose projection is perspective
        // (w taken from view-space z: _44 == 0, _34 != 0, either handedness / any z scale) and whose
        // viewport covers most of the screen (skips shadow / reflection / portrait passes).
        if (!cameraThisFrame_)
        {
            TryCaptureCamera(1);
            if (tMask != 0)
                perf_.drawCamera += Qpc() - tMask;
        }
    }
    auto plugin::Qpc(void) -> double
    {
        static const double freq = [] {
            LARGE_INTEGER f{};
            ::QueryPerformanceFrequency(&f);
            return static_cast<double>(f.QuadPart);
        }();
        LARGE_INTEGER c{};
        ::QueryPerformanceCounter(&c);
        return static_cast<double>(c.QuadPart) / freq;
    }

    void plugin::PerfStart(double minutes)
    {
        const size_t n = modules_.size();
        perf_              = Perf{};
        perf_.on           = true;
        perf_.sessionStart = perf_.startedAt = Qpc();
        perf_.sessionLen   = minutes * 60.0;
        for (auto* v : {&perf_.frame, &perf_.render, &perf_.text, &perf_.packet, &perf_.cur, &perf_.peak, &perf_.totalModule, &perf_.worstModule})
            v->assign(n, 0.0);
        const std::string dir = std::string(core_->GetInstallPath()) + "\\logs";
        ::CreateDirectoryA(dir.c_str(), nullptr);
        SYSTEMTIME t{};
        ::GetLocalTime(&t);
        char name[96]{};
        ::sprintf_s(name, "\\farplaneui_perf_%04u-%02u-%02u_%02u%02u%02u.log", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        perf_.file = dir + name;
        char b[512]{};
        ::sprintf_s(b, "FarplaneUI v0.11.3 perf log - %.1f minutes, one entry per 5 seconds. Times in ms.\r\n"
                       "gap = time between frames (the game's frame time; over 50 ms counts as a hitch). ours = FarplaneUI work in that frame.\r\n"
                       "modules: average / worst single frame (Frame + Render; text and packet time are in the average).\r\n\r\n",
            minutes);
        PerfWrite(b);
        char m[160]{};
        ::sprintf_s(m, "FarplaneUI perf: logging for %.1f minutes to ", minutes);
        Print(m + perf_.file + " (/fpui perf stop to end early)");
    }

    void plugin::PerfWrite(const std::string& text)
    {
        FILE* f = nullptr;
        if (::fopen_s(&f, perf_.file.c_str(), "ab") == 0 && f != nullptr)
        {
            ::fwrite(text.data(), 1, text.size(), f);
            ::fclose(f);
        }
    }

    // One log entry for the last 5 seconds, then reset the window.
    void plugin::PerfWindow(void)
    {
        auto& p             = perf_;
        const double now    = Qpc();
        const double frames = std::max(1.0, p.frames);
        SYSTEMTIME t{};
        ::GetLocalTime(&t);
        char b[512]{};
        ::sprintf_s(b, "[%02u:%02u:%02u] fps %5.1f | gap max %6.1f, hitches %d | ours avg %.3f max %.3f | draw hook %.3f/frame (%.0f calls; mask %.3f, camera %.3f)\r\n",
            t.wHour, t.wMinute, t.wSecond, p.frames / std::max(0.001, now - p.startedAt), p.gapMax * 1000.0, p.hitches, p.present * 1000.0 / frames,
            p.presentMax * 1000.0, p.draw * 1000.0 / frames, p.drawCalls / frames, p.drawMask * 1000.0 / frames, p.drawCamera * 1000.0 / frames);
        std::string line = b;
        ::sprintf_s(b, "%.0f fps, worst frame %.0f ms, %d hitches | FarplaneUI %.2f ms/frame + draw hook %.2f ms/frame", p.frames / std::max(0.001, now - p.startedAt),
            p.gapMax * 1000.0, p.hitches, p.present * 1000.0 / frames, p.draw * 1000.0 / frames);
        perfLastWindow_ = b;
        std::vector<size_t> order(modules_.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        auto total = [&](size_t i) { return p.frame[i] + p.render[i] + p.text[i] + p.packet[i]; };
        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return total(x) > total(y); });
        line += "    ";
        for (size_t k = 0; k < order.size() && k < 6; ++k)
        {
            const size_t i = order[k];
            ::sprintf_s(b, "%s %.3f/%.2f  ", modules_[i]->Name(), total(i) * 1000.0 / frames, p.peak[i] * 1000.0);
            line += b;
        }
        line += "\r\n";
        PerfWrite(line);

        // Session totals, then a fresh window.
        p.totalFrames += p.frames, p.totalPresent += p.present, p.totalHitches += p.hitches, ++p.windows;
        p.worstGap = std::max(p.worstGap, p.gapMax), p.worstOurs = std::max(p.worstOurs, p.presentMax);
        for (size_t i = 0; i < modules_.size(); ++i)
        {
            p.totalModule[i] += total(i);
            p.worstModule[i] = std::max(p.worstModule[i], p.peak[i]);
        }
        p.startedAt = now;
        p.frames = p.present = p.presentMax = p.draw = p.drawCalls = p.gapMax = p.drawMask = p.drawCamera = 0;
        p.hitches = 0;
        for (auto* v : {&p.frame, &p.render, &p.text, &p.packet, &p.peak})
            std::fill(v->begin(), v->end(), 0.0);
        if (now - p.sessionStart >= p.sessionLen)
            PerfFinish("finished");
    }

    // Session summary to the log and chat.
    void plugin::PerfFinish(const char* why)
    {
        auto& p = perf_;
        if (!p.on)
            return;
        p.on                = false;
        const double frames = std::max(1.0, p.totalFrames);
        char b[512]{};
        ::sprintf_s(b, "\r\n== %s: %d windows, %.0f frames | hitches (over 50 ms) %d, worst gap %.1f | ours avg %.3f, worst %.3f ==\r\n", why,
            p.windows, p.totalFrames, p.totalHitches, p.worstGap * 1000.0, p.totalPresent * 1000.0 / frames, p.worstOurs * 1000.0);
        std::string text = b;
        std::vector<size_t> order(modules_.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return p.totalModule[x] > p.totalModule[y]; });
        for (const size_t i : order)
        {
            ::sprintf_s(b, "  %-11s avg %.3f  worst frame %.2f\r\n", modules_[i]->Name(), p.totalModule[i] * 1000.0 / frames, p.worstModule[i] * 1000.0);
            text += b;
        }
        text += "If hitches are high while 'ours' stays small, the stutter comes from outside FarplaneUI.\r\n";
        PerfWrite(text);
        ::sprintf_s(b, "FarplaneUI perf %s: %d hitches, worst gap %.0f ms, our avg %.2f ms/frame (worst %.1f ms). Log: ", why, p.totalHitches,
            p.worstGap * 1000.0, p.totalPresent * 1000.0 / frames, p.worstOurs * 1000.0);
        perfLastSummary_ = b + p.file;
        Print(perfLastSummary_);
    }

    // Settings > Performance: start / stop the logger, live numbers, recent logs.
    void plugin::RenderPerfSettings(IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx_);
        const std::string dir = std::string(core_->GetInstallPath()) + "\\logs";
        g.Section("Performance log");
        if (perf_.on)
        {
            const double el = Qpc() - perf_.sessionStart;
            char b[160]{};
            ::sprintf_s(b, "Logging: %d:%02d of %d:%02d, %d hitches so far", static_cast<int>(el) / 60, static_cast<int>(el) % 60,
                static_cast<int>(perf_.sessionLen) / 60, static_cast<int>(perf_.sessionLen) % 60, perf_.totalHitches + perf_.hitches);
            g.Info("Status", b);
            g.Info("File", perf_.file.c_str());
            if (g.Button("Stop", "End the log now and write the summary.", "Stop"))
                PerfFinish("stopped");
        }
        else
        {
            g.Info("Status", "Idle");
            g.Slider("Duration", "How long to record, in minutes.", perfMinutes_, 1.0f, 60.0f, "%.0f min");
            if (g.Button("Start", "Record frame times and per-module cost to the logs folder (every 5 seconds).", "Start"))
                PerfStart(perfMinutes_);
        }
        g.Info("Last 5 seconds", perfLastWindow_.empty() ? "-" : perfLastWindow_.c_str());
        g.Info("Last result", perfLastSummary_.empty() ? "-" : perfLastSummary_.c_str());

        g.Section("Logs");
        if (g.Button("Folder", dir.c_str(), "Open logs folder"))
            ::ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        // Most recent logs first (names sort by date and time).
        std::vector<std::string> logs;
        WIN32_FIND_DATAA fd{};
        if (HANDLE h = ::FindFirstFileA((dir + "\\farplaneui_perf_*.log").c_str(), &fd); h != INVALID_HANDLE_VALUE)
        {
            do
                logs.emplace_back(fd.cFileName);
            while (::FindNextFileA(h, &fd));
            ::FindClose(h);
        }
        std::sort(logs.rbegin(), logs.rend());
        for (size_t i = 0; i < logs.size() && i < 8; ++i)
        {
            gui->PushID(static_cast<int>(i));
            if (g.Button(logs[i].c_str(), "Open in your text editor.", "Open"))
                ::ShellExecuteA(nullptr, "open", (dir + "\\" + logs[i]).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            gui->PopID();
        }
        if (logs.empty())
            g.Info("Logs", "None yet. Start a log above, or type /fpui perf.");
    }

    auto plugin::TryCaptureCamera(int source) -> bool
    {
        if (device_ == nullptr)
            return false;
        // Only take the camera from opaque world geometry: depth test + depth write on, alpha blending
        // off, and either fixed-function XYZ+NORMAL vertices or the game's position/normal model shader.
        // Shadow, sky, particle and UI passes fail this, so they can't hand us a wrong camera.
        // (Same draw filter xiuinative uses; not counted as "rejected" since most draws fail it.)
        {
            DWORD zEnable = 0, zWrite = 0, alpha = 0, vs = 0;
            if (FAILED(device_->GetRenderState(D3DRS_ZENABLE, &zEnable)) || FAILED(device_->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite)) ||
                FAILED(device_->GetRenderState(D3DRS_ALPHABLENDENABLE, &alpha)) || zEnable == 0 || zWrite == 0 || alpha != 0)
                return false;
            if (FAILED(device_->GetVertexShader(&vs)))
                return false;
            if ((vs & 1) == 0)
            {
                if ((vs & D3DFVF_POSITION_MASK) != D3DFVF_XYZ || (vs & D3DFVF_NORMAL) == 0)
                    return false;
            }
            else
            {
                static const DWORD kWorldDecl[] = {
                    D3DVSD_STREAM(0),
                    D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3),
                    D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3),
                    D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR),
                    D3DVSD_REG(D3DVSDE_TEXCOORD0, D3DVSDT_FLOAT2),
                    D3DVSD_REG(D3DVSDE_POSITION2, D3DVSDT_FLOAT3),
                    D3DVSD_END(),
                };
                DWORD decl[16]{};
                DWORD size = sizeof(decl);
                if (FAILED(device_->GetVertexShaderDeclaration(vs, decl, &size)) || size != sizeof(kWorldDecl) ||
                    std::memcmp(decl, kWorldDecl, sizeof(kWorldDecl)) != 0)
                    return false;
            }
        }
        D3DMATRIX proj{};
        if (FAILED(device_->GetTransform(D3DTS_PROJECTION, &proj)))
            return false;
        // Perspective: w = +-z_view (_44 == 0, _34 != 0), no ortho / UI matrices.
        if (!(std::abs(proj._44) < 0.001f && std::abs(proj._34) > 0.001f && std::abs(proj._11) > 0.05f && std::abs(proj._22) > 0.05f))
        {
            ++ctx_.camera.rejProj;
            ++ctx_.camera.rejected;
            return false;
        }
        D3DMATRIX view{};
        D3DVIEWPORT8 vp{};
        if (FAILED(device_->GetTransform(D3DTS_VIEW, &view)) || FAILED(device_->GetViewport(&vp)) || vp.Width == 0 || vp.Height == 0)
            return false;
        // No render-target / aspect test: the 3D scene is drawn into an offscreen target whose size is
        // FFXI's Background Resolution (often square, e.g. 4096x4096), then stretched to the screen.
        // Shadow / sky / UI passes are already excluded by the opaque-geometry filter above.
        // Reject identity / untranslated views (UI or sky passes).
        {
            const bool ident = std::abs(view._11 - 1) < 1e-4f && std::abs(view._22 - 1) < 1e-4f && std::abs(view._33 - 1) < 1e-4f &&
                               std::abs(view._41) < 1e-3f && std::abs(view._42) < 1e-3f && std::abs(view._43) < 1e-3f;
            const bool noTrans = std::abs(view._41) + std::abs(view._42) + std::abs(view._43) < 0.01f;
            if (ident || noTrans)
            {
                ++ctx_.camera.rejView;
                ++ctx_.camera.rejected;
                return false;
            }
        }
        ctx_.camera.lastFrame = ctx_.camera.frame;
        ctx_.camera.proj     = proj;
        ctx_.camera.view     = view;
        ctx_.camera.viewport = vp;
        ctx_.camera.valid    = true;
        ctx_.camera.source   = source;
        ++ctx_.camera.captures;
        cameraThisFrame_ = true;
        // Remember the scene target and the game's depth test for the 3D ring (worldring.cpp).
        IDirect3DSurface8* scene = nullptr;
        if (SUCCEEDED(device_->GetRenderTarget(&scene)) && scene != nullptr)
        {
            ctx_.camera.sceneTarget = scene;
            scene->Release();
        }
        DWORD zFunc = D3DCMP_LESSEQUAL;
        if (SUCCEEDED(device_->GetRenderState(D3DRS_ZFUNC, &zFunc)))
            ctx_.camera.zFunc = zFunc;
        return true;
    }
    auto plugin::Direct3DDrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) -> bool
    {
        OnDraw();
        return false;
    }
    auto plugin::Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT) -> bool
    {
        OnDraw();
        return false;
    }
    auto plugin::Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, CONST void*, UINT) -> bool
    {
        OnDraw();
        return false;
    }
    auto plugin::Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT) -> bool
    {
        OnDraw();
        return false;
    }

    // Configuration window: header bar, module list on the left, the module's sections next to it,
    // and the selected section's settings as a two-column grid of cards (cfgui.hpp).
    void plugin::RenderSettingsWindow(IGuiManager* gui)
    {
        // Controller: the Open button (touchpad) opens / closes the settings; any controller action while they
        // are open hands the window to the controller (and holds its buttons from the game) until it closes.
        if (pad::Pressed(pad::Open))
        {
            show_settings_window_ = !show_settings_window_;
            padNav_               = show_settings_window_;
            cfg::Nav().focus      = 0;
        }
        else if (show_settings_window_ && !padNav_)
            for (int a = 0; a < pad::kActions; ++a)
                padNav_ |= pad::Pressed(static_cast<pad::Action>(a));
        bool padPage = false; // the controller changed the category / sub-tab this frame
        if (show_settings_window_ && padNav_)
        {
            const int pages = static_cast<int>(modules_.size()) + 1; // + Performance
            if (pad::Pressed(pad::Cancel))
                show_settings_window_ = false;
            if (pad::Pressed(pad::NextPage))
                settingsModule_ = (settingsModule_ + 1) % pages, settingsSection_ = 0, padPage = true;
            if (pad::Pressed(pad::PrevPage))
                settingsModule_ = (settingsModule_ + pages - 1) % pages, settingsSection_ = 0, padPage = true;
            if (pad::Pressed(pad::NextSection))
                ++settingsSection_, padPage = true;
            if (pad::Pressed(pad::PrevSection))
                settingsSection_ = std::max(0, settingsSection_ - 1), padPage = true;
            if (padPage)
                cfg::Nav().focus = 0;
        }
        if (!show_settings_window_)
            padNav_ = false;
        pad::SetCapture(show_settings_window_ && padNav_);
        ctx_.settingsOpen = show_settings_window_;
        if (!show_settings_window_)
        {
            ctx_.settingsModule = nullptr;
            cfg::NavFrame(false);
            return;
        }
        const Palette& c = *ctx_.palette;
        ScopedTheme theme(gui, c);
        settingsModule_ = std::clamp(settingsModule_, 0, static_cast<int>(modules_.size())); // == size(): Performance page
        const bool perfPage = settingsModule_ == static_cast<int>(modules_.size());
        IModule* mod        = perfPage ? nullptr : modules_[static_cast<size_t>(settingsModule_)].get();
        const auto sections = mod != nullptr ? mod->SettingsSections() : std::vector<const char*>{};
        settingsSection_    = std::clamp(settingsSection_, 0, std::max(0, static_cast<int>(sections.size()) - 1));

        // Ember Glass style over the whole window (incoming/ember_wiring2 WIRING.md C1, [ember_dock.config]).
        using ember::Hex;
        const Rgba ivory = Hex(0xf3e9d8ff), muted = Hex(0xb9ad9cff), dim = Hex(0x9a8f80ff), amber = Hex(0xe6b479ff), emberC = Hex(0xcf733eff);
        struct VarF
        {
            ImGuiStyleVar v;
            float f;
        };
        struct VarV
        {
            ImGuiStyleVar v;
            ImVec2 x;
        };
        struct Col
        {
            ImGuiCol c;
            uint32_t hex;
        };
        static const VarF kVarF[] = {{ImGuiStyleVar_WindowRounding, 10}, {ImGuiStyleVar_ChildRounding, 8}, {ImGuiStyleVar_FrameRounding, 11},
            {ImGuiStyleVar_PopupRounding, 8}, {ImGuiStyleVar_GrabRounding, 3}, {ImGuiStyleVar_ScrollbarRounding, 3}, {ImGuiStyleVar_WindowBorderSize, 1},
            {ImGuiStyleVar_ChildBorderSize, 0}, {ImGuiStyleVar_FrameBorderSize, 1}, {ImGuiStyleVar_ScrollbarSize, 6}, {ImGuiStyleVar_GrabMinSize, 6}};
        static const VarV kVarV[] = {{ImGuiStyleVar_FramePadding, {10, 4}}, {ImGuiStyleVar_ItemSpacing, {8, 6}}, {ImGuiStyleVar_ItemInnerSpacing, {6, 4}}};
        static const Col kCols[] = {{ImGuiCol_WindowBg, 0x161a30e1}, {ImGuiCol_ChildBg, 0x1c2140c8}, {ImGuiCol_PopupBg, 0x161a30f2},
            {ImGuiCol_Border, 0xe6b47996}, {ImGuiCol_Separator, 0xe6b4792d}, {ImGuiCol_Text, 0xf3e9d8ff}, {ImGuiCol_TextDisabled, 0x9a8f80ff},
            {ImGuiCol_FrameBg, 0x0a0c18d2}, {ImGuiCol_FrameBgHovered, 0x20263fff}, {ImGuiCol_FrameBgActive, 0x2a2f4aff}, {ImGuiCol_SliderGrab, 0xe6b479ff},
            {ImGuiCol_SliderGrabActive, 0xfff3e0ff}, {ImGuiCol_Button, 0x0a0c18d2}, {ImGuiCol_ButtonHovered, 0xcf733e55}, {ImGuiCol_ButtonActive, 0xcf733eaa},
            {ImGuiCol_Header, 0xcf733e46}, {ImGuiCol_HeaderHovered, 0xcf733e30}, {ImGuiCol_HeaderActive, 0xcf733e80}, {ImGuiCol_CheckMark, 0xe6b479ff},
            {ImGuiCol_ScrollbarBg, 0x00000000}, {ImGuiCol_ScrollbarGrab, 0xe6b4795a}, {ImGuiCol_TitleBg, 0x161a30e1}, {ImGuiCol_TitleBgActive, 0x161a30e1}};
        for (const auto& v : kVarF)
            gui->PushStyleVar(v.v, v.f);
        for (const auto& v : kVarV)
            gui->PushStyleVar(v.v, v.x);
        for (const auto& c2 : kCols)
            gui->PushStyleColor(c2.c, ToVec4(Hex(c2.hex)));
        constexpr int kNumVars = static_cast<int>(std::size(kVarF) + std::size(kVarV)), kNumCols = static_cast<int>(std::size(kCols));

        gui->SetNextWindowSize(ImVec2(1000, 563), ImGuiCond_FirstUseEver);
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const bool open = gui->Begin("##fpui_config", &show_settings_window_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                                                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        gui->PopStyleVar();
        if (open)
        {
            const ImVec2 wp = gui->GetWindowPos();
            const float ww = gui->GetWindowSize().x, hh = 42.0f;
            // Top sheen, header text, version pill, round close button, divider.
            ui::ImageAt(gui, ctx_.white, ImVec2(wp.x + 12, wp.y + 1), ImVec2(ww - 24, 1), ImVec2(0, 0), ImVec2(1, 1), Hex(0xfff0dc2d));
            const float ty = wp.y + (hh - gui->GetTextLineHeight()) * 0.5f;
            ember::Text(gui, ImVec2(wp.x + 18, ty), amber, "CONFIGURATION");
            float x = wp.x + 18 + gui->CalcTextSize("CONFIGURATION").x + 10;
            ember::Text(gui, ImVec2(x, ty), muted, "Farplane UI");
            x += gui->CalcTextSize("Farplane UI").x + 8;
            char ver[16]{};
            ::sprintf_s(ver, "%.2f", GetVersion());
            const ImVec2 vs(gui->CalcTextSize(ver).x + 16, gui->GetTextLineHeight() + 2);
            ember::RoundRect(gui, ImVec2(x, wp.y + (hh - vs.y) * 0.5f), vs, Hex(0x0a0c18d2), Hex(0xe6b47937), vs.y * 0.5f, 1.0f);
            ember::TextCenter(gui, x + vs.x * 0.5f, ty, amber, ver);
            const float cb = 22.0f;
            const ImVec2 cp(wp.x + ww - 18 - cb, wp.y + (hh - cb) * 0.5f);
            gui->SetCursorScreenPos(cp);
            const bool closeHit = gui->InvisibleButton("##fpui_close", ImVec2(cb, cb));
            ember::RoundRect(gui, cp, ImVec2(cb, cb), gui->IsItemHovered() ? Hex(0xcf733e80) : Hex(0x0a0c18d2), Hex(0xe6b47996), cb * 0.5f, 1.0f);
            ember::TextCenter(gui, cp.x + cb * 0.5f, cp.y + (cb - gui->GetTextLineHeight()) * 0.5f, amber, "x");
            if (closeHit)
                show_settings_window_ = false;
            ui::ImageAt(gui, ctx_.white, ImVec2(wp.x + 12, wp.y + hh), ImVec2(ww - 24, 1), ImVec2(0, 0), ImVec2(1, 1), Hex(0xe6b4792d));

            // Category / subtab columns sized from their longest label (+32; min 160 / 110).
            float catW = 160.0f, subW = 110.0f;
            for (const auto& m : modules_)
                catW = std::max(catW, gui->CalcTextSize(m->Title()).x + 32.0f);
            catW = std::max(catW, gui->CalcTextSize("Performance").x + 32.0f);
            for (const char* sname : sections)
                subW = std::max(subW, gui->CalcTextSize(sname).x + 32.0f);
            const float bodyY = wp.y + hh + 10, footH = 22.0f;
            gui->SetCursorScreenPos(ImVec2(wp.x + 10, bodyY));
            gui->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            gui->PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
            gui->PushStyleColor(ImGuiCol_Header, ToVec4(Rgba{0, 0, 0, 0}));
            gui->PushStyleColor(ImGuiCol_HeaderActive, ToVec4(Rgba{0, 0, 0, 0}));
            // One category / subtab row; the selected look is drawn here, hover comes from HeaderHovered.
            auto row = [&](const char* label, const char* id, bool sel, float h, bool pill) -> bool {
                const ImVec2 p = gui->GetCursorScreenPos();
                const float w  = gui->GetContentRegionAvail().x;
                if (sel && pill)
                    ember::RoundRect(gui, p, ImVec2(w, h), emberC, Rgba{0, 0, 0, 0}, 12.0f, 0.0f);
                else if (sel)
                {
                    ember::RoundRect(gui, p, ImVec2(w, h), Hex(0xcf733e46), Hex(0xe6b47978), 7.0f, 1.0f);
                    ui::ImageAt(gui, ctx_.white, ImVec2(p.x + 4, p.y + (h - 15) * 0.5f), ImVec2(3, 15), ImVec2(0, 0), ImVec2(1, 1), emberC);
                }
                gui->PushStyleColor(ImGuiCol_Text, ToVec4(sel ? (pill ? Hex(0x1a1410ff) : ivory) : muted));
                char sid[96]{};
                ::sprintf_s(sid, "   %s##%s", label, id);
                const bool hit = gui->Selectable(sid, false, 0, ImVec2(0, h));
                gui->PopStyleColor();
                return hit;
            };
            const float bodyH = gui->GetWindowSize().y - (bodyY - wp.y) - footH - 10;
            if (gui->BeginChild("##fpui_cfg_nav", ImVec2(catW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                for (size_t i = 0; i < modules_.size(); ++i)
                {
                    char id[16]{};
                    ::sprintf_s(id, "nav%u", static_cast<unsigned>(i));
                    const bool sel = static_cast<int>(i) == settingsModule_;
                    if (sel && padPage)
                        gui->SetScrollHereY(0.5f);
                    if (row(modules_[i]->Title(), id, sel, 25.0f, false) && !sel)
                        settingsModule_ = static_cast<int>(i), settingsSection_ = 0;
                }
                if (row("Performance", "navperf", perfPage, 25.0f, false) && !perfPage)
                    settingsModule_ = static_cast<int>(modules_.size()), settingsSection_ = 0;
            }
            gui->EndChild();
            gui->SameLine(0.0f, 8.0f);
            if (gui->BeginChild("##fpui_cfg_sections", ImVec2(subW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                for (size_t i = 0; i < sections.size(); ++i)
                {
                    char id[16]{};
                    ::sprintf_s(id, "sec%u", static_cast<unsigned>(i));
                    if (row(sections[i], id, static_cast<int>(i) == settingsSection_, 24.0f, true))
                        settingsSection_ = static_cast<int>(i);
                }
            }
            gui->EndChild();
            gui->SameLine(0.0f, 8.0f);
            gui->PopStyleColor(2);

            // Settings.
            ctx_.settingsModule = mod;
            gui->PushStyleColor(ImGuiCol_ChildBg, ToVec4(Rgba{0, 0, 0, 0}));
            if (gui->BeginChild("##fpui_cfg_content", ImVec2(gui->GetContentRegionAvail().x - 10, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                ctx_.settingsSection = settingsSection_;
                cfg::NavFrame(padNav_);
                if (mod != nullptr)
                    mod->RenderSettings(ctx_, gui);
                else
                    RenderPerfSettings(gui);
                if (mod == chat_ && settingsSection_ == 0)
                {
                    cfg::Grid g(gui, ctx_);
                    bool on = ember::On();
                    if (g.Toggle("Ember Dock", "One glass dock for the bottom modules (off: the classic framed panels). /fpui theme ember on|off", on))
                    {
                        ember::On() = on;
                        MarkDirty();
                    }
                    if (ember::On() && g.Slider("Dock height", "Height of the Ember dock in pixels at 1080p (scales with the screen).", ember::T().dockH, 140.0f, 600.0f, "%.0f"))
                        MarkDirty();
                    float pct = chatWidth_ * 100.0f;
                    if (g.Slider("Docked width", "Chat panel width when docked, as a share of the screen.", pct, 15.0f, 80.0f, "%.0f%%"))
                    {
                        chatWidth_ = pct / 100.0f;
                        MarkDirty();
                    }
                }
            }
            gui->EndChild();
            gui->PopStyleColor();
            gui->PopStyleVar(2);
            // Footer hint.
            gui->SetCursorScreenPos(ImVec2(wp.x + 18, wp.y + gui->GetWindowSize().y - footH - 2));
            if (padNav_)
                gui->TextColored(ToVec4(dim), "D-pad: move / change   |   Confirm: toggle / press   |   L1 / R1: category   |   L2 / R2: sub-tab   |   Cancel: close");
            else
                gui->TextColored(ToVec4(dim), "Hold - / + to repeat   |   Shift = x10   |   Ctrl+click a slider to type a value");
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
        gui->PopStyleColor(kNumCols);
        gui->PopStyleVar(kNumVars);
    }

    void plugin::ReportStatus(void)
    {
        Print(std::string("FarplaneUI v0.11.3 build ") + __DATE__ + " " + __TIME__ + " loaded (/fpui status for diagnostics). /fpui opens the settings. Modules: mask, chat, minimap, inventory, party, equip, target, targetbar, points, ticks, notify, recast, castbar, status, enemies, readycheck, alliance, compass, cmdmenu, bars, dps, quests, shadows, native.");
        mask_->Report(ctx_);
        std::string missingArt;
        int ok = 0, total = 0;
        for (const char* n : {"panel_9slice", "bar_trough_9slice", "fill_hp_green", "fill_hp_red", "fill_mp_blue", "fill_tp_gold"})
        {
            ++total;
            if (ctx_.Art(n) != nullptr)
                ++ok;
            else
                missingArt += (missingArt.empty() ? "" : ", ") + std::string(n);
        }
        Print("art kit: " + std::to_string(ok) + "/" + std::to_string(total) + " PNGs in " + ctx_.resourceDir + "artkit\\" +
              (missingArt.empty() ? std::string() : " (missing: " + missingArt + " -> color-only fallback)"));
    }
} // namespace farplaneui

__declspec(dllexport) auto __stdcall expCreatePlugin(const char* args) -> IPlugin*
{
    UNREFERENCED_PARAMETER(args);
    return new farplaneui::plugin();
}

__declspec(dllexport) auto __stdcall expDestroyPlugin(void* instance) -> void
{
    delete static_cast<farplaneui::plugin*>(instance);
}

__declspec(dllexport) auto __stdcall expGetInterfaceVersion(void) -> double
{
    return ASHITA_INTERFACE_VERSION;
}
