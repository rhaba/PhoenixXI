/**
 * Farplane UI - Ember Dock theme helpers (see ember.hpp).
 */
#include "ember.hpp"

#include <algorithm>

namespace farplaneui::ember
{
    auto T(void) -> Theme&
    {
        static Theme theme;
        return theme;
    }
    auto On(void) -> bool&
    {
        static bool on = true;
        return on;
    }

    auto Tex(Context& ctx, const char* name, UINT* w, UINT* h) -> IDirect3DTexture8*
    {
        if (ctx.textures == nullptr)
            return nullptr;
        return ctx.textures->File(ctx.resourceDir + "ember_dock\\" + name + ".png", w, h);
    }

    namespace
    {
        // Horizontal 3-slice: caps keep the texture's round ends (texture cap = half its height).
        void Slice3(IGuiManager* gui, IDirect3DTexture8* tex, UINT tw, UINT th, const ImVec2& pos, const ImVec2& size, const Rgba& tint, float alpha)
        {
            const float cap  = std::min(size.y * 0.5f, size.x * 0.5f);
            const float ucap = static_cast<float>(th) * 0.5f / static_cast<float>(std::max<UINT>(1, tw));
            ui::ImageAt(gui, tex, pos, ImVec2(cap, size.y), ImVec2(0, 0), ImVec2(ucap, 1), tint, alpha);
            if (size.x > cap * 2)
                ui::ImageAt(gui, tex, ImVec2(pos.x + cap, pos.y), ImVec2(size.x - cap * 2, size.y), ImVec2(ucap, 0), ImVec2(1 - ucap, 1), tint, alpha);
            ui::ImageAt(gui, tex, ImVec2(pos.x + size.x - cap, pos.y), ImVec2(cap, size.y), ImVec2(1 - ucap, 0), ImVec2(1, 1), tint, alpha);
        }
        void Flat(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, const Rgba& c, float alpha)
        {
            if (ctx.white != nullptr)
                ui::ImageAt(gui, ctx.white, pos, size, ImVec2(0, 0), ImVec2(1, 1), c, alpha);
        }
    } // namespace

    void Glass(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float alpha)
    {
        UINT tw = 0, th = 0;
        if (IDirect3DTexture8* t = Tex(ctx, "glass_panel_9slice", &tw, &th); t != nullptr)
            ui::NineSlice(gui, t, tw, th, pos, size, 11.0f, std::min(T().S(11), std::min(size.x, size.y) * 0.5f), Rgba{1, 1, 1, 1}, alpha);
        else
            Flat(ctx, gui, pos, size, T().glass, alpha);
    }
    void Pill(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, const Rgba& tint, float alpha)
    {
        UINT tw = 0, th = 0;
        IDirect3DTexture8* t = Tex(ctx, size.y <= 8 ? "pill_fill_white_8" : "pill_fill_white_16", &tw, &th);
        if (t != nullptr)
            Slice3(gui, t, tw, th, pos, size, tint, alpha);
        else
            Flat(ctx, gui, pos, size, tint, alpha);
    }
    void Track(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float alpha)
    {
        UINT tw = 0, th = 0;
        if (IDirect3DTexture8* t = Tex(ctx, "pill_track_16", &tw, &th); t != nullptr)
            Slice3(gui, t, tw, th, pos, size, Rgba{1, 1, 1, 1}, alpha);
        else
            Flat(ctx, gui, pos, size, T().track, alpha);
    }
    void Bar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const Rgba& fill, TrackKind track, float alpha)
    {
        // ImGui's own progress bar with full rounding: a clean vector pill at any size (the stretched
        // pill textures looked uneven on short, wide bars). Overlay "##" = no text.
        (void)ctx;
        frac = std::clamp(frac, 0.0f, 1.0f);
        if (size.x < 1 || size.y < 1)
            return;
        const Theme& t = T();
        auto a = [&](Rgba c) { c.a *= alpha; return ToVec4(c); };
        const Rgba trackCol = track == TrackKind::Dark ? t.track : track == TrackKind::Faint ? t.faintTrack : Rgba{0, 0, 0, 0};
        const ImVec2 keep   = gui->GetCursorScreenPos();
        gui->PushStyleVar(ImGuiStyleVar_FrameRounding, size.y * 0.5f);
        gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, track == TrackKind::Dark ? 1.0f : 0.0f);
        gui->PushStyleColor(ImGuiCol_FrameBg, a(trackCol));
        gui->PushStyleColor(ImGuiCol_Border, a(t.edgeDim));
        gui->PushStyleColor(ImGuiCol_PlotHistogram, a(fill));
        gui->SetCursorScreenPos(pos);
        gui->ProgressBar(frac, size, "##");
        gui->PopStyleColor(3);
        gui->PopStyleVar(2);
        gui->SetCursorScreenPos(keep);
    }
    void RoundRect(IGuiManager* gui, const ImVec2& pos, const ImVec2& size, const Rgba& fill, const Rgba& edge, float radius, float border)
    {
        if (size.x < 1 || size.y < 1)
            return;
        const ImVec2 keep = gui->GetCursorScreenPos();
        gui->PushStyleVar(ImGuiStyleVar_FrameRounding, radius);
        gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, border);
        gui->PushStyleColor(ImGuiCol_FrameBg, ToVec4(fill));
        gui->PushStyleColor(ImGuiCol_Border, ToVec4(edge));
        gui->SetCursorScreenPos(pos);
        gui->ProgressBar(0.0f, size, "##"); // frame only: fill + border, nothing inside
        gui->PopStyleColor(2);
        gui->PopStyleVar(2);
        gui->SetCursorScreenPos(keep);
    }
    void Slot(Context& ctx, IGuiManager* gui, const ImVec2& pos, float size, bool filled)
    {
        UINT tw = 0, th = 0;
        if (IDirect3DTexture8* t = Tex(ctx, filled ? "slot_filled" : "slot_empty", &tw, &th); t != nullptr)
            ui::NineSlice(gui, t, tw, th, pos, ImVec2(size, size), 9.0f, std::min(T().S(8), size * 0.5f), Rgba{1, 1, 1, 1});
        else
            Flat(ctx, gui, pos, ImVec2(size, size), T().glassDeep, 1.0f);
    }
    void RoundFrame(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size)
    {
        UINT tw = 0, th = 0;
        if (IDirect3DTexture8* t = Tex(ctx, "minimap_frame_9slice", &tw, &th); t != nullptr)
            ui::NineSlice(gui, t, tw, th, pos, size, 11.0f, std::min(T().S(11), std::min(size.x, size.y) * 0.5f), Rgba{1, 1, 1, 1});
    }
    void Text(IGuiManager* gui, const ImVec2& pos, const Rgba& c, const char* s)
    {
        gui->SetCursorScreenPos(pos);
        gui->TextColored(ToVec4(c), "%s", s);
    }
    void TextRight(IGuiManager* gui, float right, float y, const Rgba& c, const char* s)
    {
        Text(gui, ImVec2(right - gui->CalcTextSize(s).x, y), c, s);
    }
    void TextCenter(IGuiManager* gui, float cx, float y, const Rgba& c, const char* s)
    {
        Text(gui, ImVec2(cx - gui->CalcTextSize(s).x * 0.5f, y), c, s);
    }

    void BuildDock(Context& ctx, std::vector<Quad>& out)
    {
        out.clear();
        const Layout& L = ctx.layout;
        const Theme& t  = T();
        if (L.dock.w <= 0 || L.dock.h <= 0)
            return;
        // Glass 9-slice over the dock rect.
        UINT tw = 0, th = 0;
        if (IDirect3DTexture8* g = Tex(ctx, "glass_panel_9slice", &tw, &th); g != nullptr && tw > 0 && th > 0)
        {
            const float sm = 11.0f, dm = t.S(11);
            const float us = sm / static_cast<float>(tw), vs = sm / static_cast<float>(th);
            const float xs[4] = {L.dock.x, L.dock.x + dm, L.dock.x + L.dock.w - dm, L.dock.x + L.dock.w};
            const float ys[4] = {L.dock.y, L.dock.y + dm, L.dock.y + L.dock.h - dm, L.dock.y + L.dock.h};
            const float us4[4] = {0, us, 1 - us, 1}, vs4[4] = {0, vs, 1 - vs, 1};
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    out.push_back({g, xs[c], ys[r], xs[c + 1] - xs[c], ys[r + 1] - ys[r], us4[c], vs4[r], us4[c + 1], vs4[r + 1], Rgba{1, 1, 1, 1}});
        }
        else if (ctx.white != nullptr)
            out.push_back({ctx.white, L.dock.x, L.dock.y, L.dock.w, L.dock.h, 0, 0, 1, 1, t.glass});
        // Dividers: 1px amber, fading out toward both ends.
        if (ctx.white == nullptr)
            return;
        const float G = t.S(t.gap), y0 = L.chat.y + t.S(t.dividerInset), y1 = L.chat.y + L.chat.h - t.S(t.dividerInset);
        const float xs[] = {L.minimap.x - G * 0.5f, L.dps.x - G * 0.5f, L.dps.x + L.dps.w + t.S(t.laneGapLeft) * 0.5f, L.equip.x - G * 0.5f};
        constexpr int kSeg = 12;
        for (const float x : xs)
            for (int i = 0; i < kSeg; ++i)
            {
                const float a = static_cast<float>(i) / kSeg, b = static_cast<float>(i + 1) / kSeg;
                const float mid = (a + b) * 0.5f;
                const float k   = 1.0f - std::abs(mid - 0.5f) * 2.0f; // 0 at the ends, 1 in the middle
                Rgba c          = t.amber;
                c.a             = t.dividerAlpha * k;
                out.push_back({ctx.white, std::floor(x), y0 + (y1 - y0) * a, 1.0f, (y1 - y0) * (b - a), 0, 0, 1, 1, c});
            }
    }
} // namespace farplaneui::ember
