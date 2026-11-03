/**
 * Farplane UI - helpers shared by the built-in modules.
 */
#ifndef FARPLANEUI_MODULES_COMMON_HPP_INCLUDED
#define FARPLANEUI_MODULES_COMMON_HPP_INCLUDED
#pragma once

#include "../cfgui.hpp"
#include "../modules.hpp"

namespace farplaneui::mod
{
    // Begins a themed, art-framed window. Always pair with EndPanel() (even when this returns false).
    // Frame: art kit panel_9slice when the PNG loads, otherwise a 1px ember border (color-only fallback).
    struct Panel
    {
        IGuiManager* gui{nullptr};
        bool open{false};
        int colors{0}, vars{0};
    };
    auto BeginPanel(Context& ctx, IGuiManager* gui, const char* id, bool* show, bool docked, const Rect& dock, const Rect& user, bool locked,
        float bgAlpha, bool useArt, ImGuiWindowFlags extra = 0, bool border = true) -> Panel;
    void EndPanel(Panel& p, Rect* user, bool docked, Context& ctx);
    // Strip panels above the band (target bar, points) share one look so they line up: a text row
    // over a thin bar. StripBar draws the bar across the content width at the cursor.
    constexpr float kStripBarH = 6.0f;
    void StripBar(Context& ctx, IGuiManager* gui, float frac, const Rgba& fill);

    // Bar styles (bars.cpp): one place draws every progress bar so the Bar Style page restyles them all.
    struct BarStyle
    {
        int shape{0}; // 0 classic, 1 framed, 2 pill, 3 chamfer
        int fill{0};  // ember, twilight, gilded, misty, hatch, stone
        bool tint{true}, animate{true};
        bool timers{true}, meters{false};
    };
    auto Bars(void) -> BarStyle&;
    // meter = HP/MP/TP-style bar (vs timers / cast bar); `color` is the bar's meaning colour.
    void DrawBar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const Rgba& color, float alpha, bool meter);
    // One of the ready-made strips (combos\<shape>_track + <shape>_<fill>), cropped to frac.
    void ComboBar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const char* shape, const char* fill, float alpha = 1.0f);
    void LoadBars(IConfigurationManager* cfg, const char* alias);
    void SaveBars(IConfigurationManager* cfg, const char* alias);
    void BarSettings(Context& ctx, IGuiManager* gui);
    // The player's status effects, read from memory (IPlayer status icons + timers). A timer is the
    // expiry in 1/60 s on the game's UTC clock: seconds left = (raw - (utc - 0x3C307D70) * 60) / 60;
    // 0x7FFFFFFF = permanent (seconds < 0).
    struct Buff
    {
        uint16_t id{0};
        uint32_t slot{0};
        double seconds{-1};
    };
    // Earth time (Unix seconds) from the game's own clock; the PC clock if that can't be read.
    auto EarthTime(void) -> uint32_t;
    // Fixed objects (doors, counters, home points, moogles, ???, elevators: entity type 2+) that aren't
    // monsters or players. Target overlays skip them unless ShowObjects() is on (Target settings).
    auto IsObject(IEntity* ent, uint32_t index) -> bool;
    auto ShowObjects(void) -> bool&;
    auto PlayerBuffs(Context& ctx) -> std::vector<Buff>;
    // Entity indices (1..0x3FF) that are monsters (spawn flag 0x10). Rescanned at most 5x a second and
    // shared by the minimap and enemy list, so neither walks the whole entity table every frame.
    // Callers still check render flags / HP per frame on this short list.
    auto NearbyMobs(Context& ctx) -> const std::vector<uint16_t>&;
    // Name of the top-most native game menu ("menu    cmdmenu " style, trimmed), empty when none is open
    // (e.g. the command menu, trade, item lists). Signature used by many Ashita addons (XITools).
    auto TopMenuName(void) -> std::string;
    // The game has hidden its own interface (cutscenes): 1 yes, 0 no, -1 unknown (pattern not found).
    auto GameInterfaceHidden(void) -> int;
    // Address of that menu's header (name at +0x46), 0 when none is open. Debug / experiments.
    auto TopMenuHeader(void) -> uintptr_t;
    // The live menu object (header pointer at +4), 0 when none is open. Debug / experiments.
    auto TopMenuObject(void) -> uintptr_t;
    auto BuffName(Context& ctx, uint16_t id) -> std::string;
    auto StatusIcon(Context& ctx, uint16_t id) -> IDirect3DTexture8*;
    // Common docked/lock/show subcommands. Returns true if handled.
    auto DockCommand(Context& ctx, const char* name, const std::vector<std::string>& args, bool& show, bool& docked, bool& locked) -> bool;
    void DockSettings(cfg::Grid& g, Context& ctx, bool& show, bool& docked, bool& locked);
    void LoadDock(IConfigurationManager* cfg, const char* alias, const char* sec, bool& show, bool& docked, bool& locked, Rect& user);
    void SaveDock(IConfigurationManager* cfg, const char* alias, const char* sec, bool show, bool docked, bool locked, const Rect& user);
} // namespace farplaneui::mod

#endif
