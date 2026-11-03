/**
 * Farplane UI - Utsusemi shadows (incoming ember_dock_wiring (2) WIRING.md "Utsusemi effect" U1-U7).
 *
 * Your Copy Image count, drawn around your character inside the 3D scene by the host (worldring.cpp,
 * the same pass as the target ring) in one of five styles: wisps, diamond, rune ring, smoke, pips, or
 * none. This module reads the count from your buffs, runs the lost / recast / fade animation (U4) and
 * fills ctx.shadows every frame. Settings live in [shadows] and [shadows.<style>].
 */
#include "common.hpp"

#include <chrono>
#include <cmath>

namespace farplaneui
{
    namespace
    {
        auto NowS(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        // Copy Image status ids: 66 = 1 shadow, 444 = 2, 445 = 3, 446 = 4 or more.
        auto CopyImage(int16_t id) -> int
        {
            switch (id)
            {
            case 66: return 1;
            case 444: return 2;
            case 445: return 3;
            case 446: return 4;
            default: return 0;
            }
        }
        constexpr int kColors = 6;
        constexpr const char* kColorNames[kColors] = {"Ember", "Amber", "Violet", "Cyan", "Ivory", "Indigo"};
        constexpr Rgba kColorValues[kColors]       = {hex(0xcf733e), hex(0xe6b479), hex(0x9b7cf0), hex(0xbff4f0), hex(0xf3e8d8), hex(0x3a3570)};
        constexpr const char* kDesc[ShadowModule::kStyles] = {
            "No shadow display.",
            "Glowing motes orbit your chest with a fading trail, one per shadow, over a faint ring on the ground.",
            "A slowly turning crystal around you; each side facet is one shadow and goes dark when it is hit.",
            "A circle on the ground with one rune per shadow. The easiest to read in a busy fight.",
            "Soft smoke clones beside you, one per shadow; a clone disperses when it takes a hit.",
            "A small row of diamonds above your head. The least visual noise.",
        };
        // Approximate standing height in yalms by race (1-2 Hume, 3-4 Elvaan, 5-6 Tarutaru, 7 Mithra, 8 Galka).
        auto RaceHeight(uint8_t race) -> float
        {
            switch (race)
            {
            case 3: case 4: return 1.75f;
            case 5: case 6: return 1.0f;
            case 7: return 1.5f;
            case 8: return 2.0f;
            default: return 1.6f;
            }
        }
        auto Ease(float t) -> float { return 1.0f - (1.0f - t) * (1.0f - t); }
    } // namespace

    ShadowModule::ShadowModule(void)
    {
        // Per-style defaults: colour (kColorValues index), speed in rad/s (0 for styles that don't move).
        look_[1] = {2, 1.0f, 1.0f, 0.9f, true};  // wisps: violet
        look_[2] = {0, 1.0f, 1.0f, 0.35f, true}; // diamond: ember
        look_[3] = {0, 1.0f, 1.0f, 0.0f, true};  // rune ring
        look_[4] = {5, 1.0f, 1.0f, 0.5f, false}; // smoke: indigo
        look_[5] = {0, 1.0f, 1.0f, 0.0f, true};  // pips
    }

    void ShadowModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "shadows";
        const std::string st    = ui::Lower(ui::GetS(cfg, alias, S, "style", kKeys[style_]));
        for (int i = 0; i < kStyles; ++i)
            if (st == kKeys[i])
                style_ = i;
        lostTime_        = std::clamp(cfg->GetFloat(alias, S, "lostTime", lostTime_), 0.1f, 2.0f);
        outTime_         = std::clamp(cfg->GetFloat(alias, S, "outTime", outTime_), 0.1f, 3.0f);
        maxShown_        = std::clamp(cfg->GetInt32(alias, S, "maxShown", maxShown_), 3, 6);
        hideFirstPerson_ = cfg->GetBool(alias, S, "hideFirstPerson", hideFirstPerson_);
        for (int i = 1; i < kStyles; ++i)
        {
            const std::string sec = std::string("shadows.") + kKeys[i];
            Look& l               = look_[static_cast<size_t>(i)];
            l.color   = std::clamp(cfg->GetInt32(alias, sec.c_str(), "color", l.color), 0, kColors - 1);
            l.size    = std::clamp(cfg->GetFloat(alias, sec.c_str(), "size", l.size), 0.5f, 2.0f);
            l.opacity = std::clamp(cfg->GetFloat(alias, sec.c_str(), "opacity", l.opacity), 0.0f, 1.0f);
            l.speed   = std::clamp(cfg->GetFloat(alias, sec.c_str(), "speed", l.speed), 0.0f, 2.0f);
            l.glow    = cfg->GetBool(alias, sec.c_str(), "glow", l.glow);
        }
    }
    void ShadowModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "shadows";
        cfg->SetValue(alias, S, "style", kKeys[style_]);
        ui::SetF(cfg, alias, S, "lostTime", lostTime_);
        ui::SetF(cfg, alias, S, "outTime", outTime_);
        ui::SetI(cfg, alias, S, "maxShown", maxShown_);
        ui::SetB(cfg, alias, S, "hideFirstPerson", hideFirstPerson_);
        for (int i = 1; i < kStyles; ++i)
        {
            const std::string sec = std::string("shadows.") + kKeys[i];
            const Look& l         = look_[static_cast<size_t>(i)];
            ui::SetI(cfg, alias, sec.c_str(), "color", l.color);
            ui::SetF(cfg, alias, sec.c_str(), "size", l.size);
            ui::SetF(cfg, alias, sec.c_str(), "opacity", l.opacity);
            ui::SetF(cfg, alias, sec.c_str(), "speed", l.speed);
            ui::SetB(cfg, alias, sec.c_str(), "glow", l.glow);
        }
    }

    // Shadows on you now (U1), or the preview count.
    auto ShadowModule::Count(Context& ctx) const -> int
    {
        if (previewCount_ >= 0)
            return previewCount_;
        if (preview_ && ctx.settingsOpen)
            return 4 - static_cast<int>(std::fmod(NowS() - previewAt_, 7.5) / 1.5); // 4, 3, 2, 1, 0 every 1.5 s
        auto* mm     = ctx.core != nullptr ? ctx.core->GetMemoryManager() : nullptr;
        auto* player = mm != nullptr ? mm->GetPlayer() : nullptr;
        const int16_t* b = player != nullptr ? player->GetBuffs() : nullptr;
        int n = 0;
        for (int i = 0; b != nullptr && i < 32; ++i)
            n = std::max(n, CopyImage(b[i]));
        return n;
    }

    void ShadowModule::Frame(Context& ctx)
    {
        const double now = NowS();
        const float dt   = static_cast<float>(std::clamp(now - lastAt_, 0.0, 0.1));
        lastAt_          = now;
        const int n      = std::min(Count(ctx), maxShown_);
        // U4: the slot at index n was just lost; all gone -> fade and grow; count up -> fade the new ones in.
        if (n < prev_)
            lostIdx_ = n, lostT_ = 0.0f;
        if (prev_ > 0 && n == 0)
            outT_ = 0.0f;
        if (n > prev_)
            inT_ = 0.0f, outT_ = 99.0f;
        prev_ = n;
        lostT_ += dt, outT_ += dt, inT_ += dt;
        const Look& l = look_[static_cast<size_t>(style_)];
        spin_ += dt * l.speed;
        if (spin_ > 6283.0f)
            spin_ = std::fmod(spin_, 6.2831853f);

        if (style_ == 0 || !ctx.camera.Fresh() || (n == 0 && outT_ >= outTime_))
            return;
        auto* mm    = ctx.core != nullptr ? ctx.core->GetMemoryManager() : nullptr;
        auto* ent   = mm != nullptr ? mm->GetEntity() : nullptr;
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (ent == nullptr || party == nullptr)
            return;
        const uint32_t me = party->GetMemberTargetIndex(0);
        if (me == 0 || me >= 2304 || ent->GetRawEntity(me) == nullptr || (ent->GetRenderFlags0(me) & 0x4200u) != 0x200u)
            return; // not drawn by the game (zoning, cutscene)
        auto& s  = ctx.shadows;
        s.x      = ent->GetLocalPositionX(me), s.y = ent->GetLocalPositionZ(me), s.z = ent->GetLocalPositionY(me); // game world (X, height, Y)
        s.height = RaceHeight(ent->GetRace(me));
        if (hideFirstPerson_)
        {
            // Camera position from the view matrix; first person = the camera sits in your head.
            const auto& v  = ctx.camera.view;
            const float cx = -(v._41 * v._11 + v._42 * v._12 + v._43 * v._13);
            const float cy = -(v._41 * v._21 + v._42 * v._22 + v._43 * v._23);
            const float cz = -(v._41 * v._31 + v._42 * v._32 + v._43 * v._33);
            const float dx = cx - s.x, dy = cy - (s.y - s.height * 0.9f), dz = cz - s.z;
            if (dx * dx + dy * dy + dz * dz < 0.8f * 0.8f)
                return;
        }
        s.want     = true;
        s.style    = style_;
        s.time     = spin_;
        s.count    = n;
        s.maxShown = maxShown_;
        s.lostIdx  = lostT_ < lostTime_ ? lostIdx_ : -1;
        s.lostA    = 1.0f - std::min(1.0f, lostT_ / lostTime_);
        s.inA      = std::min(1.0f, inT_ / 0.25f);
        const float o = n == 0 ? std::min(1.0f, outT_ / outTime_) : 0.0f;
        s.outA     = 1.0f - o;
        s.outScale = 1.0f + 0.12f * Ease(o);
        s.color    = kColorValues[l.color];
        s.size     = l.size;
        s.opacity  = l.opacity;
        s.glow     = l.glow;
    }

    void ShadowModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = false;
        switch (ctx.settingsSection)
        {
        default:
        case 0: // Style
        {
            int s = style_;
            if (g.Choice("Shadow display", kDesc[style_], s, kNames, kStyles))
                style_ = s, ch = true;
            if (g.Toggle("Preview", "While this window is open, cycle 4 -> 0 shadows every 1.5 s so you can try the styles outside combat.", preview_))
                previewAt_ = NowS();
            break;
        }
        case 1: // Look
        {
            if (style_ == 0)
            {
                g.Info("Style", "Off. Pick a style under Style to change its look.");
                break;
            }
            Look& l = look_[static_cast<size_t>(style_)];
            g.Info("Editing", kNames[style_]);
            ch |= g.Choice("Colour", "Main colour of this style.", l.color, kColorNames, kColors);
            ch |= g.Slider("Size", "Size around your character.", l.size, 0.5f, 2.0f, "%.2f");
            ch |= g.Slider("Opacity", "How strongly it draws.", l.opacity, 0.0f, 1.0f, "%.2f");
            if (style_ != 3 && style_ != 5) // rune ring and pips don't move
                ch |= g.Slider("Speed", "Spin / drift speed in radians per second.", l.speed, 0.0f, 2.0f, "%.2f");
            ch |= g.Toggle("Glow", "Soft glow around the lines and lit shapes.", l.glow);
            break;
        }
        case 2: // Behaviour
        {
            ch |= g.Slider("Lost flash", "How long a shadow flashes when it absorbs a hit (seconds).", lostTime_, 0.1f, 2.0f, "%.2f");
            ch |= g.Slider("Fade out", "How long the effect fades after the last shadow goes (seconds).", outTime_, 0.1f, 3.0f, "%.2f");
            ch |= g.Slider("Max shown", "Shadow slots drawn. The game reports 4 for four or more (Utsusemi: San).", maxShown_, 3, 6);
            ch |= g.Toggle("Hide in first person", "Don't draw shadows while the camera is in your character's head.", hideFirstPerson_);
            break;
        }
        }
        if (ch)
            ctx.MarkDirty();
    }

    auto ShadowModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "style" && args.size() > 1)
        {
            const std::string want = ui::Lower(args[1]);
            for (int i = 0; i < kStyles; ++i)
                if (want == kKeys[i] || (i == 0 && want == "off"))
                {
                    style_ = i;
                    ctx.MarkDirty();
                    ctx.Print(std::string("FarplaneUI shadows: ") + kNames[i] + ".");
                    return true;
                }
            ctx.Print("FarplaneUI shadows: styles are none, wisps, diamond, runering, smoke, pips.");
            return true;
        }
        if (sub == "preview")
        {
            int n = -1;
            if (args.size() > 1 && ui::ParseInt(args[1], n))
                previewCount_ = std::clamp(n, 0, 6);
            else
                previewCount_ = -1;
            ctx.Print(previewCount_ >= 0 ? "FarplaneUI shadows: previewing " + std::to_string(previewCount_) + " (/fpui shadows preview to stop)."
                                         : std::string("FarplaneUI shadows: preview off."));
            return true;
        }
        ctx.Print(std::string("FarplaneUI shadows: ") + kNames[style_] + ". /fpui shadows style <none|wisps|diamond|runering|smoke|pips>, /fpui shadows preview <0-4>.");
        return true;
    }
} // namespace farplaneui
