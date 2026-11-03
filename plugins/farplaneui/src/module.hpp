/**
 * Farplane UI - module framework.
 *
 * Every feature (mask, party, chat, minimap, inventory, equipment) is an IModule owned by the
 * plugin host. The host forwards Ashita events to each module in registration order and gives
 * them a shared Context: the Ashita core, the active palette, art/texture helpers, chat output,
 * a "settings changed" callback and the dock Layout (slots inside the bottom UI mask).
 *
 * Porting a Lua addon = writing one module. See README "Adding a module".
 */

#ifndef FARPLANEUI_MODULE_HPP_INCLUDED
#define FARPLANEUI_MODULE_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/std::max under MSVC (C2589)
#endif
#include "Ashita.h"
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "theme.hpp"

#include <map>
#include <string>
#include <vector>

namespace farplaneui
{
    // ---------------------------------------------------------------------------------------------
    // Art kit palette (artkit.json / ember-navy)
    // ---------------------------------------------------------------------------------------------
    namespace art
    {
        inline constexpr Rgba navy          = hex(0x272e40);
        inline constexpr Rgba twilight_dark = hex(0x181c34);
        inline constexpr Rgba ember         = hex(0xcf733e);
        inline constexpr Rgba amber         = hex(0xe6b479);
        inline constexpr Rgba gold_hi       = hex(0xf6da93);
        inline constexpr Rgba orb_cyan      = hex(0xbff4f0);
    } // namespace art

    struct Rect
    {
        float x{0}, y{0}, w{0}, h{0};
        auto valid(void) const -> bool { return w > 1.0f && h > 1.0f; }
    };

    // Slots inside the bottom mask, recomputed every frame by the host from the band edges.
    // Left -> right: chat | minimap | (free) | inventory | party | equipment.
    // Strip just above the band, centred: target bar | resting ticks | points.
    struct Layout
    {
        float screenW{1920}, screenH{1080};
        float bandTop{0}, bandBottom{0};
        bool bandVisible{false};
        Rect chat, minimap, inventory, party, equip, dps;
        Rect dock;     // Ember Dock: the glass dock holding the band modules (w = 0 when off)
        Rect buffLane; // Ember Dock: party effects lane between the DPS column and the party
        Rect targetbar, ticks, points;
    };

    // Small D3DX texture cache for art PNGs (artkit/, assets/) and in-memory images (item icons).
    class TextureCache
    {
        struct Entry
        {
            IDirect3DTexture8* tex{nullptr};
            UINT w{0}, h{0};
            bool tried{false};
        };
        IDirect3DDevice8* device_{nullptr};
        std::map<std::string, Entry> files_;
        std::map<uint32_t, Entry> mem_;

    public:
        void Initialize(IDirect3DDevice8* device) { device_ = device; }
        void Release(void);
        // Loads once; returns nullptr if missing (callers fall back to color-only drawing).
        auto File(const std::string& path, UINT* w = nullptr, UINT* h = nullptr) -> IDirect3DTexture8*;
        // Image file bytes in memory (e.g. IItem::Bitmap, a BMP), keyed by id.
        auto Memory(uint32_t key, const void* data, uint32_t size) -> IDirect3DTexture8*;
        auto LoadedFiles(void) const -> std::vector<std::pair<std::string, bool>>;
    };

    // Last perspective camera seen in the game's draw calls (captured by the host).
    struct Camera
    {
        D3DMATRIX view{}, proj{};
        D3DVIEWPORT8 viewport{};
        bool valid{false};
        int source{0};          // 0 none, 1 draw-hook capture, 2 device state at Present
        uint32_t captures{0};   // successful captures (debug)
        uint32_t rejected{0};   // world draws rejected by the checks below (debug; sum of the rej* counters)
        uint32_t rejProj{0};    // projection not perspective
        uint32_t rejView{0};    // identity or untranslated view
        uint32_t frame{0};      // Present counter
        uint32_t lastFrame{0};  // frame of the last good capture
        // Render target the world geometry was drawn into (the offscreen scene; compared, never used)
        // and the depth test the game used for it (the projection is reversed, so don't assume LESS).
        const void* sceneTarget{nullptr};
        DWORD zFunc{D3DCMP_LESSEQUAL};
        auto Fresh(void) const -> bool { return valid && frame - lastFrame <= 30; }
    };

    // Ground ring requested by the target module for the host to draw inside the 3D scene.
    // Position is in game world space (Ashita X, Z = height, Y); yaw as IEntity reports it.
    struct WorldRing
    {
        bool want{false};
        float x{0}, y{0}, z{0};
        float radius{1.0f}, yaw{0};
        Rgba color{1, 1, 1, 1};
        bool arrow{true};
        // Ember ring (WIRING.md T1-T7); widths in screen px, converted with pxWorld (yalms per pixel).
        int style{0};                 // 0 hairline, 1 HP arc, 2 classic band
        float pxWorld{0.02f}, alpha{1.0f};
        float lineWidth{2.5f}, arcWidth{4.0f}, trackWidth{5.0f};
        int glowPasses{3};
        float glowStep{2.5f}, glowAlpha{120.0f / 255.0f}, innerAlpha{22.0f / 255.0f};
        float notch{0.1222f}, chevronLen{0.14f}, chevronWidth{0.11f}; // notch: 7 degrees
        bool rear{true}, rearIn{false};
        float rearArc{0.7854f}, rearAlpha{170.0f / 255.0f}, rearBoost{1.5f}; // 45 degrees
        float hp{1.0f}, hpGhost{1.0f}, ghostAlpha{0.0f};
        Rgba tickColor{1, 1, 1, 1};
        float pulseAge{-1.0f}, pulseHz{1.2f}, pulseGrow{0.25f};
        uint32_t drawnFrame{0xFFFFFFFFu}; // host: last frame the ring was drawn in 3D (0xFFFFFFFF = never)
        bool hooked{false};               // host: SetRenderTarget hook installed
    };

    struct Context
    {
        Camera camera{};
        WorldRing ring{};
        int settingsSection{0}; // section selected in the settings window (IModule::SettingsSections)
        bool settingsOpen{false};                // the settings window is showing
        bool commandMenuMoved{false};
        // Render target tracking (worldring.cpp SetRenderTarget hook): the back buffer surface and
        // whether it is the current target. Lets the per-draw-call hooks avoid querying D3D.
        const void* backBuffer{nullptr};
        bool targetIsBackBuffer{true}, targetKnown{false};            // the command menu module is relocating the game's command menu
        const class IModule* settingsModule{nullptr}; // module selected in it
        std::string mapGrid;    // player's map grid square ("H-10"), set by the minimap; empty if unknown
        IAshitaCore* core{nullptr};
        IDirect3DDevice8* device{nullptr};
        const Palette* palette{&kThemes[kDefaultTheme]};
        TextureCache* textures{nullptr};
        Layout layout{};
        std::string resourceDir; // <Ashita>\resources\farplaneui\  (trailing backslash)
        std::string alias;       // IConfigurationManager alias for the shared ini
        IDirect3DTexture8* hfade{nullptr};
        IDirect3DTexture8* white{nullptr};

        // Provided by the host.
        void (*printFn)(void* host, const std::string& text){nullptr};
        bool (*commandFn)(void* host, const char* text){nullptr}; // direct /fpui routing (safety net)
        void (*dirtyFn)(void* host){nullptr};
        void* host{nullptr};

        void Print(const std::string& text) const
        {
            if (printFn != nullptr)
                printFn(host, text);
        }
        void MarkDirty(void) const
        {
            if (dirtyFn != nullptr)
                dirtyFn(host);
        }
        auto Art(const char* name, UINT* w = nullptr, UINT* h = nullptr) const -> IDirect3DTexture8*
        {
            return textures != nullptr ? textures->File(resourceDir + "artkit\\" + name + ".png", w, h) : nullptr;
        }
    };

    /**
     * Module interface. Override only what you need; every hook has a no-op default.
     *   Name()            short id, also the ini section and the /fpui <name> command prefix
     *   Initialize()      after the plugin loads (settings already loaded)
     *   Direct3DInit()    device available: create textures/fonts
     *   Load()/Save()     read/write your [section] through IConfigurationManager
     *   Frame()           once per frame before rendering (state updates)
     *   Render()          ImGui windows (inside Direct3DPresent)
     *   RenderSettings()  your part of the /fpui settings window (a tab)
     *   Command()         /fpui <Name()> args...  (args[0] is the first word after the module name)
     *   IncomingText()    return true to block the line from the native chat log
     *   IncomingPacket()  read-only packet tap
     */
    class IModule
    {
    public:
        virtual ~IModule(void) = default;
        virtual auto Name(void) const -> const char* = 0;
        virtual auto Title(void) const -> const char* { return Name(); }
        virtual void Initialize(Context& ctx) { (void)ctx; }
        virtual void Release(void) {}
        virtual void Direct3DInit(Context& ctx) { (void)ctx; }
        virtual void Load(IConfigurationManager* cfg, const char* alias) { (void)cfg; (void)alias; }
        virtual void Save(IConfigurationManager* cfg, const char* alias) const { (void)cfg; (void)alias; }
        virtual void Frame(Context& ctx) { (void)ctx; }
        virtual void Render(Context& ctx, IGuiManager* gui) { (void)ctx; (void)gui; }
        // Settings window: sections listed in the second column (ctx.settingsSection = the selected one).
        virtual auto SettingsSections(void) const -> std::vector<const char*> { return {"General"}; }
        virtual void RenderSettings(Context& ctx, IGuiManager* gui) { (void)ctx; (void)gui; }
        virtual auto Command(Context& ctx, const std::vector<std::string>& args) -> bool { (void)ctx; (void)args; return false; }
        virtual auto IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool
        {
            (void)ctx; (void)mode; (void)message; (void)injected;
            return false;
        }
        virtual void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
        {
            (void)ctx; (void)id; (void)size; (void)data;
        }
    };

    // ---------------------------------------------------------------------------------------------
    // Shared helpers (ui.cpp)
    // ---------------------------------------------------------------------------------------------
    namespace ui
    {
        auto Lower(std::string s) -> std::string;
        auto ParseInt(const std::string& s, int& out) -> bool;
        auto ParseFloat(const std::string& s, float& out) -> bool;
        auto Join(const std::vector<std::string>& v, size_t from, const char* sep = " ") -> std::string;
        auto Tex(IDirect3DTexture8* t) -> ImTextureRef;
        void Tooltip(IGuiManager* gui, const char* text);
        void Section(IGuiManager* gui, const Palette& c, const char* label);
        void AccentLine(IGuiManager* gui, const Context& ctx, const Rgba& color);
        void Bar(IGuiManager* gui, const Palette& c, float frac, const Rgba& fill, float height, const char* overlay);
        // Image at an absolute screen position without disturbing the layout cursor.
        void ImageAt(IGuiManager* gui, IDirect3DTexture8* tex, const ImVec2& pos, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1,
            const Rgba& tint, float alpha = 1.0f);
        // 9-slice frame over [pos, pos+size]; srcMargin in texture pixels, dstMargin on screen.
        auto NineSlice(IGuiManager* gui, IDirect3DTexture8* tex, UINT tw, UINT th, const ImVec2& pos, const ImVec2& size, float srcMargin,
            float dstMargin, const Rgba& tint, float alpha = 1.0f) -> bool;
        // Applies a docked rect (Always) or the user rect (FirstUseEver), plus lock flags.
        auto DockWindow(IGuiManager* gui, bool docked, const Rect& dock, const Rect& user, bool locked) -> ImGuiWindowFlags;
        // Reads a free window's rect back so it can be persisted. Returns true if it changed.
        auto ReadBack(IGuiManager* gui, Rect& user) -> bool;
        void SaveRect(IConfigurationManager* cfg, const char* alias, const char* section, const char* prefix, const Rect& r);
        void LoadRect(IConfigurationManager* cfg, const char* alias, const char* section, const char* prefix, Rect& r);
        void SetF(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, float v);
        void SetI(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, int v);
        void SetB(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, bool v);
        auto GetS(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, const std::string& def) -> std::string;
        auto ColorToHex(const Rgba& c) -> std::string;
        auto HexToColor(const std::string& s, Rgba& out) -> bool;
    } // namespace ui

} // namespace farplaneui

#endif
