/**
 * Farplane UI - Ember Dock theme (incoming/ember_dock: WIRING.md, theme_ember_dock.ini).
 *
 * One glass dock along the bottom holds chat | minimap | DPS (+ bag gauges) | party | equipment,
 * with faded dividers between them; the strip panels above it are glass too. The dock itself is
 * drawn by the mask's under-UI sprite pass (so it sits under every window); module contents draw
 * with the texture helpers here. Ashita's ImGui wrapper exposes no ImDrawList, so rounded shapes
 * are textures: glass 9-slice, pill track / white pill fill (tinted), equipment slots.
 * Textures: <Ashita>\resources\farplaneui\ember_dock\. Every helper falls back to flat quads.
 */
#pragma once

#include "module.hpp"
#include "mask.hpp"

namespace farplaneui::ember
{
    constexpr auto Hex(uint32_t rgba) -> Rgba
    {
        return Rgba{static_cast<float>((rgba >> 24) & 0xFF) / 255.0f, static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
            static_cast<float>((rgba >> 8) & 0xFF) / 255.0f, static_cast<float>(rgba & 0xFF) / 255.0f};
    }

    // theme_ember_dock.ini values, px at 1080p (scaled by screenH / 1080).
    struct Theme
    {
        Rgba glass{Hex(0x161a30da)}, glassDeep{Hex(0x101324c8)}, track{Hex(0x0a0c18c8)}, edge{Hex(0xe6b47996)}, edgeDim{Hex(0xe6b4793c)};
        Rgba ember{Hex(0xcf733eff)}, emberLow{Hex(0xac4519ff)}, amber{Hex(0xe6b479ff)}, ivory{Hex(0xf3e9d8ff)}, muted{Hex(0xb9ad9cff)};
        Rgba dim{Hex(0xa89c88ff)}, mp{Hex(0x6ec8cdff)}, ok{Hex(0x7cc9b6ff)}, bad{Hex(0xe5482fff)}, timestamp{Hex(0xce966bff)};
        Rgba tabIdle{Hex(0x272e40b4)}, faintTrack{Hex(0xffffff19)}, rowSep{Hex(0xe6b47912)};
        float margin{10}, padding{12}, gap{16}, dividerInset{4}, dividerAlpha{90.0f / 255.0f}, radius{10};
        float chatW{700}, tabH{18}, tabPadX{9}, tabGap{6}, inputH{24};
        // v2 (buff lane): DPS narrowed, bag gauges stacked under it, six-row party.
        float dpsW{200}, dpsHeaderH{18}, dpsRowH{20};
        float invH{19}, invGap{10}, invStackGap{3}, warnAt{0.85f}, fullAt{1.0f};
        float partyW{300}, nameW{104}, hpH{14}, hpY{3}, mpTpY{20}, mpTpH{2.5f}, mpSplit{0.62f}, lowHp{0.30f}, buffSize{16}, buffGap{2};
        // Buff lane ([ember_dock.buffs])
        float laneGapLeft{16}, laneGapRight{10}, iconSize{18}, iconCompact{14}, iconGap{2}, overflowExtra{8};
        float timerH{2}, timerGap{2}, timerLowAt{0.15f}, expireAt{0.12f}, expireSec{10};
        int popupMaxCols{8};
        Rgba debuffFill{Hex(0x3a1218e6)}, debuffEdge{Hex(0xe5482fc8)}, buffFill{Hex(0x101324e6)}, buffEdge{Hex(0xe6b4793c)};
        Rgba expireGlow{Hex(0xffb34796)}, overflowFill{Hex(0xcf733ee6)}, overflowEdge{Hex(0xe6b479ff)}, overflowText{Hex(0x1a1020ff)};
        float cell{32}, cellGap{4}, gilH{20};
        float stripGap{8};  // dock top -> strip panels
        float dockH{235};   // dock height incl. the bottom margin ([layout] dockSize; mockup 196)
        float scale{1.0f};
        auto S(float v) const -> float { return v * scale; }
    };
    auto T(void) -> Theme&;
    auto On(void) -> bool&; // [layout] emberDock

    auto Tex(Context& ctx, const char* name, UINT* w = nullptr, UINT* h = nullptr) -> IDirect3DTexture8*;

    // Inside an ImGui window (absolute screen positions).
    void Glass(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float alpha = 1.0f);
    void Pill(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, const Rgba& tint, float alpha = 1.0f); // solid, tinted
    void Track(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float alpha = 1.0f);                 // dark + dim edge
    // Pill bar: track (dark, or `faint` = thin white track, or none) and a fill never narrower than its height.
    enum class TrackKind : uint8_t { Dark, Faint, None };
    void Bar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const Rgba& fill, TrackKind track = TrackKind::Dark,
        float alpha = 1.0f);
    void Slot(Context& ctx, IGuiManager* gui, const ImVec2& pos, float size, bool filled);
    // Rounded rectangle with an optional border (ImGui frame, so any radius / colour).
    void RoundRect(IGuiManager* gui, const ImVec2& pos, const ImVec2& size, const Rgba& fill, const Rgba& edge, float radius, float border);
    // Rounded frame over a square (minimap): glass-coloured outside corners + edge line.
    void RoundFrame(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size);
    void Text(IGuiManager* gui, const ImVec2& pos, const Rgba& c, const char* s);
    void TextRight(IGuiManager* gui, float right, float y, const Rgba& c, const char* s);
    void TextCenter(IGuiManager* gui, float cx, float y, const Rgba& c, const char* s);

    // The dock, as sprite quads for the mask's under-UI pass.
    using Quad = SpriteQuad;
    void BuildDock(Context& ctx, std::vector<Quad>& out);
} // namespace farplaneui::ember
