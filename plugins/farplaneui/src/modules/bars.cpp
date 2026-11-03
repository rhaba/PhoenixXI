/**
 * Farplane UI - styled bars (resources\farplaneui\bars\, see assets/bars/README in the art drop).
 *
 * Shapes:
 *   Classic   the flat look: dark border, dark track, solid colour fill.
 *   Framed    frame_bar_9slice.png (3px margins) with fill_<style>.png inside. With "colour by meaning"
 *             the light fill_<style>_white.png is multiplied by the bar's own colour (HP green / amber /
 *             red, spells blue...), so meaning survives the style. hatch and stone tile (u = width / 256);
 *             hatch scrolls.
 *   Pill /    the ready-made 256x20 strips: combos\<shape>_track.png, then combos\<shape>_<style>.png
 *   Chamfer   cropped to the progress, both 3-sliced horizontally (pill 10px, chamfer 5px end caps).
 *             These carry their own colours.
 * Timers / cast bar and HP-style meters can use the style independently.
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui::mod
{
    namespace
    {
        constexpr const char* kFills[]  = {"ember", "twilight", "gilded", "misty", "hatch", "stone"};
        constexpr const char* kShapes[] = {"Classic", "Framed", "Pill", "Chamfer"};

        auto Seconds(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        // Horizontal 3-slice of a strip texture over [pos, pos+size], drawn only up to `crop` pixels.
        void Slice3(IGuiManager* gui, IDirect3DTexture8* tex, float texW, const ImVec2& pos, const ImVec2& size, float srcMargin, float crop,
            const Rgba& tint, float alpha)
        {
            const float m = std::min(srcMargin * size.y / 20.0f, size.x * 0.5f); // caps keep their aspect
            const float d[4] = {0.0f, m, size.x - m, size.x};
            const float s[4] = {0.0f, srcMargin, texW - srcMargin, texW};
            for (int i = 0; i < 3; ++i)
            {
                const float d0 = d[i], d1 = std::min(d[i + 1], crop);
                if (d1 <= d0)
                    break;
                const float u1 = s[i] + (s[i + 1] - s[i]) * (d1 - d0) / std::max(0.001f, d[i + 1] - d0);
                ui::ImageAt(gui, tex, ImVec2(pos.x + d0, pos.y), ImVec2(d1 - d0, size.y), ImVec2(s[i] / texW, 0), ImVec2(u1 / texW, 1), tint, alpha);
            }
        }
    } // namespace

    auto Bars(void) -> BarStyle&
    {
        static BarStyle style;
        return style;
    }

    void DrawBar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const Rgba& color, float alpha, bool meter)
    {
        const BarStyle& st = Bars();
        frac = std::clamp(frac, 0.0f, 1.0f);
        const bool styled = st.shape != 0 && (meter ? st.meters : st.timers);
        const std::string dir = ctx.resourceDir + "bars\\";
        const char* fill = kFills[std::clamp(st.fill, 0, 5)];

        if (styled && (st.shape == 2 || st.shape == 3))
        {
            const char* shape = st.shape == 2 ? "pill" : "chamfer";
            UINT tw = 0, th = 0, fw = 0, fh = 0;
            IDirect3DTexture8* track = ctx.textures->File(dir + "combos\\" + shape + "_track.png", &tw, &th);
            IDirect3DTexture8* full  = ctx.textures->File(dir + "combos\\" + shape + "_" + fill + ".png", &fw, &fh);
            if (track != nullptr && full != nullptr && tw > 0 && fw > 0)
            {
                const float margin = st.shape == 2 ? 10.0f : 5.0f;
                Slice3(gui, track, static_cast<float>(tw), pos, size, margin, size.x, Rgba{1, 1, 1, 1}, alpha);
                if (frac > 0.0f)
                    Slice3(gui, full, static_cast<float>(fw), pos, size, margin, size.x * frac, Rgba{1, 1, 1, 1}, alpha);
                return;
            }
        }
        if (styled && st.shape == 1)
        {
            UINT fw = 0, fh = 0, gw = 0, gh = 0;
            IDirect3DTexture8* frame = ctx.textures->File(dir + "frame_bar_9slice.png", &fw, &fh);
            IDirect3DTexture8* tex   = ctx.textures->File(dir + "fill_" + fill + (st.tint ? "_white" : "") + ".png", &gw, &gh);
            if (frame != nullptr && tex != nullptr)
            {
                const float m = std::min(3.0f, size.y * 0.25f);
                ui::NineSlice(gui, frame, fw, fh, ImVec2(pos.x - m, pos.y - m), ImVec2(size.x + 2 * m, size.y + 2 * m), 3.0f, m, Rgba{1, 1, 1, 1}, alpha);
                const float w = size.x * frac;
                if (w > 0.0f)
                {
                    const bool tile = st.fill == 4 || st.fill == 5; // hatch, stone
                    float u0 = 0.0f, u1 = 1.0f;
                    if (tile)
                    {
                        u1 = w / 256.0f;
                        if (st.fill == 4 && st.animate)
                        {
                            const float scroll = static_cast<float>(std::fmod(Seconds() * 24.0 / 256.0, 1.0));
                            u0 -= scroll, u1 -= scroll;
                        }
                    }
                    ui::ImageAt(gui, tex, pos, ImVec2(w, size.y), ImVec2(u0, 0), ImVec2(u1, 1), st.tint ? color : Rgba{1, 1, 1, 1}, alpha);
                }
                return;
            }
        }
        // Classic (also the fallback if the art is missing).
        ui::ImageAt(gui, ctx.white, ImVec2(pos.x - 1, pos.y - 1), ImVec2(size.x + 2, size.y + 2), ImVec2(0, 0), ImVec2(1, 1), Rgba{0.03f, 0.03f, 0.06f, 1}, 0.85f * alpha);
        ui::ImageAt(gui, ctx.white, pos, size, ImVec2(0, 0), ImVec2(1, 1), Rgba{0.20f, 0.17f, 0.22f, 1}, 0.9f * alpha);
        if (frac > 0.0f)
            ui::ImageAt(gui, ctx.white, pos, ImVec2(size.x * frac, size.y), ImVec2(0, 0), ImVec2(1, 1), color, alpha);
    }

    void ComboBar(Context& ctx, IGuiManager* gui, const ImVec2& pos, const ImVec2& size, float frac, const char* shape, const char* fill, float alpha)
    {
        const std::string dir = ctx.resourceDir + "bars\\combos\\";
        UINT tw = 0, th = 0, fw = 0, fh = 0;
        IDirect3DTexture8* track = ctx.textures->File(dir + shape + "_track.png", &tw, &th);
        IDirect3DTexture8* full  = ctx.textures->File(dir + shape + "_" + fill + ".png", &fw, &fh);
        if (track == nullptr || full == nullptr || tw == 0 || fw == 0)
        {
            DrawBar(ctx, gui, pos, size, frac, art::ember, alpha, true);
            return;
        }
        const float margin = std::string(shape) == "pill" ? 10.0f : 5.0f;
        Slice3(gui, track, static_cast<float>(tw), pos, size, margin, size.x, Rgba{1, 1, 1, 1}, alpha);
        if (frac > 0.0f)
            Slice3(gui, full, static_cast<float>(fw), pos, size, margin, size.x * std::clamp(frac, 0.0f, 1.0f), Rgba{1, 1, 1, 1}, alpha);
    }

    void LoadBars(IConfigurationManager* cfg, const char* alias)
    {
        auto& b  = Bars();
        b.shape  = std::clamp(cfg->GetInt32(alias, "bars", "shape", b.shape), 0, 3);
        b.fill   = std::clamp(cfg->GetInt32(alias, "bars", "fill", b.fill), 0, 5);
        b.tint   = cfg->GetBool(alias, "bars", "tint", b.tint);
        b.timers = cfg->GetBool(alias, "bars", "timers", b.timers);
        b.meters = cfg->GetBool(alias, "bars", "meters", b.meters);
        b.animate = cfg->GetBool(alias, "bars", "animate", b.animate);
    }
    void SaveBars(IConfigurationManager* cfg, const char* alias)
    {
        const auto& b = Bars();
        ui::SetI(cfg, alias, "bars", "shape", b.shape);
        ui::SetI(cfg, alias, "bars", "fill", b.fill);
        ui::SetB(cfg, alias, "bars", "tint", b.tint);
        ui::SetB(cfg, alias, "bars", "timers", b.timers);
        ui::SetB(cfg, alias, "bars", "meters", b.meters);
        ui::SetB(cfg, alias, "bars", "animate", b.animate);
    }
    void BarSettings(Context& ctx, IGuiManager* gui)
    {
        auto& b = Bars();
        cfg::Grid g(gui, ctx);
        static const char* fillNames[] = {"Ember", "Twilight", "Gilded", "Misty", "Hatch", "Stone"};
        bool ch = g.Choice("Shape", "Classic flat bars, the framed bar, or the pill / chamfer strips.", b.shape, kShapes, 4);
        ch |= g.Choice("Fill", "Fill style for styled bars.", b.fill, fillNames, 6);
        ch |= g.Toggle("Colour by meaning", "Framed: tint the fill with each bar's own colour (HP green/red, spells blue). Pill / chamfer keep their own colours.", b.tint);
        ch |= g.Toggle("Animate hatch", "Scroll the hatch stripes.", b.animate);
        g.Section("Use the style for");
        ch |= g.Toggle("Timers and cast bar", "Buff timers, recasts, party abilities and the cast bar.", b.timers);
        ch |= g.Toggle("HP / MP / TP bars", "Target bar, points, ticks, enemy list and alliance bars.", b.meters);
        g.Free();
        // Live preview of the current style.
        gui->Dummy(ImVec2(0, 6));
        const float w = std::min(360.0f, gui->GetContentRegionAvail().x);
        const Rgba samples[] = {Rgba{0.55f, 0.82f, 0.62f, 1}, art::amber, Rgba{0.45f, 0.62f, 0.95f, 1}, art::ember};
        const float fracs[]  = {0.85f, 0.55f, 0.7f, 0.3f};
        for (int i = 0; i < 4; ++i)
        {
            const ImVec2 p = gui->GetCursorScreenPos();
            DrawBar(ctx, gui, ImVec2(p.x + 4, p.y + 4), ImVec2(w, 16.0f), fracs[i], samples[i], 1.0f, i < 2);
            gui->Dummy(ImVec2(w + 8, 26.0f));
        }
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui::mod

namespace farplaneui
{
    void BarStyleModule::Load(IConfigurationManager* cfg, const char* alias) { mod::LoadBars(cfg, alias); }
    void BarStyleModule::Save(IConfigurationManager* cfg, const char* alias) const { mod::SaveBars(cfg, alias); }
    void BarStyleModule::RenderSettings(Context& ctx, IGuiManager* gui) { mod::BarSettings(ctx, gui); }
} // namespace farplaneui
