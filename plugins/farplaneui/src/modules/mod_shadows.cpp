/**
 * Farplane UI - Utsusemi shadows and Blink (incoming ember_dock_wiring WIRING.md "Utsusemi effect" U1-U7,
 * "Blink effect" K1-K6, "Depth" D2-D6).
 *
 * Your Copy Image count, drawn around your character inside the 3D scene by the host (worldring.cpp,
 * the same pass as the target ring) in one of five styles: wisps, diamond, rune ring, smoke, pips, or
 * none; and Blink as a shimmer of motes (cast flash-in, hit ripple, dissipate on wear-off). This module
 * reads both from your buffs (and the hit from 0x028 absorb messages), runs the animations and fills
 * ctx.shadows every frame. Settings live in [shadows], [shadows.<style>] and [shadows.blink].
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
        // Copy Image status ids: 66 = 1 shadow, 444 = 2, 445 = 3, 446 = 4 or more. Blink is 36.
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
        constexpr int16_t kBlinkId    = 36;
        constexpr uint32_t kAbsorbMsg = 31; // "<n> of <target>'s shadows absorbs the damage" (Utsusemi and Blink)
        constexpr int kColors = 7;
        constexpr const char* kColorNames[kColors] = {"Ember", "Amber", "Violet", "Cyan", "Ivory", "Indigo", "Frost"};
        constexpr Rgba kColorValues[kColors] = {hex(0xcf733e), hex(0xe6b479), hex(0x9b7cf0), hex(0xbff4f0), hex(0xf3e8d8), hex(0x3a3570), hex(0x8ce6ff)};
        constexpr const char* kDesc[ShadowModule::kStyles] = {
            "No shadow display.",
            "Glowing motes orbit your chest with a fading trail, one per shadow, over a faint ring on the ground.",
            "A slowly turning crystal around you; each side facet is one shadow and goes dark when it is hit.",
            "A circle on the ground with one rune per shadow. The easiest to read in a busy fight.",
            "Soft smoke clones beside you, one per shadow; a clone disperses when it takes a hit.",
            "A small row of diamonds above your head. The least visual noise.",
        };
        constexpr const char* kDepthNames[3] = {"Always on top", "Depth split", "Fully depth-tested"};
        constexpr const char* kDepthKeys[3]  = {"ontop", "split", "depth"};
        constexpr const char* kWhenNames[3]  = {"Dim", "Hide", "Both"};
        constexpr const char* kWhenKeys[3]   = {"dim", "hide", "both"};
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
        auto Bits(const uint8_t* d, uint32_t size, uint32_t pos, uint32_t count) -> uint32_t
        {
            uint32_t v = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t b = pos + i;
                if (b / 8 >= size)
                    break;
                v |= static_cast<uint32_t>((d[b / 8] >> (b % 8)) & 1u) << i;
            }
            return v;
        }
        auto KeyIndex(const std::string& v, const char* const* keys, int n, int def) -> int
        {
            for (int i = 0; i < n; ++i)
                if (v == keys[i])
                    return i;
            return def;
        }
        constexpr float kFlashIn = 0.35f, kRippleTime = 0.45f, kZoneSuppress = 2.0f;
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
        style_           = KeyIndex(ui::Lower(ui::GetS(cfg, alias, S, "style", kKeys[style_])), kKeys, kStyles, style_);
        lostTime_        = std::clamp(cfg->GetFloat(alias, S, "lostTime", lostTime_), 0.1f, 2.0f);
        outTime_         = std::clamp(cfg->GetFloat(alias, S, "outTime", outTime_), 0.1f, 3.0f);
        maxShown_        = std::clamp(cfg->GetInt32(alias, S, "maxShown", maxShown_), 3, 6);
        hideFirstPerson_ = cfg->GetBool(alias, S, "hideFirstPerson", hideFirstPerson_);
        depthMode_       = KeyIndex(ui::Lower(ui::GetS(cfg, alias, S, "drawOverCharacter", kDepthKeys[depthMode_])), kDepthKeys, 3, depthMode_);
        crossfade_       = std::clamp(cfg->GetFloat(alias, S, "crossfade", crossfade_), 0.02f, 0.6f);
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
        constexpr const char* B = "shadows.blink";
        blinkOn_          = cfg->GetBool(alias, B, "shimmer", blinkOn_);
        blinkColor_       = std::clamp(cfg->GetInt32(alias, B, "color", blinkColor_), 0, kColors - 1);
        blinkOpacity_     = std::clamp(cfg->GetFloat(alias, B, "opacity", blinkOpacity_), 0.0f, 1.0f);
        blinkSize_        = std::clamp(cfg->GetFloat(alias, B, "size", blinkSize_), 0.5f, 2.0f);
        blinkMotes_       = std::clamp(cfg->GetInt32(alias, B, "motes", blinkMotes_), 50, 300);
        blinkSpeed_       = std::clamp(cfg->GetFloat(alias, B, "speed", blinkSpeed_), 0.0f, 3.0f);
        blinkFadeOut_     = std::clamp(cfg->GetFloat(alias, B, "fadeOut", blinkFadeOut_), 0.2f, 3.0f);
        blinkFlashOnCast_ = cfg->GetBool(alias, B, "flashOnCast", blinkFlashOnCast_);
        blinkRipple_      = cfg->GetBool(alias, B, "hitRipple", blinkRipple_);
        whenUtsusemi_     = KeyIndex(ui::Lower(ui::GetS(cfg, alias, B, "whenUtsusemi", kWhenKeys[whenUtsusemi_])), kWhenKeys, 3, whenUtsusemi_);
    }
    void ShadowModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "shadows";
        cfg->SetValue(alias, S, "style", kKeys[style_]);
        ui::SetF(cfg, alias, S, "lostTime", lostTime_);
        ui::SetF(cfg, alias, S, "outTime", outTime_);
        ui::SetI(cfg, alias, S, "maxShown", maxShown_);
        ui::SetB(cfg, alias, S, "hideFirstPerson", hideFirstPerson_);
        cfg->SetValue(alias, S, "drawOverCharacter", kDepthKeys[depthMode_]);
        ui::SetF(cfg, alias, S, "crossfade", crossfade_);
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
        constexpr const char* B = "shadows.blink";
        ui::SetB(cfg, alias, B, "shimmer", blinkOn_);
        ui::SetI(cfg, alias, B, "color", blinkColor_);
        ui::SetF(cfg, alias, B, "opacity", blinkOpacity_);
        ui::SetF(cfg, alias, B, "size", blinkSize_);
        ui::SetI(cfg, alias, B, "motes", blinkMotes_);
        ui::SetF(cfg, alias, B, "speed", blinkSpeed_);
        ui::SetF(cfg, alias, B, "fadeOut", blinkFadeOut_);
        ui::SetB(cfg, alias, B, "flashOnCast", blinkFlashOnCast_);
        ui::SetB(cfg, alias, B, "hitRipple", blinkRipple_);
        cfg->SetValue(alias, B, "whenUtsusemi", kWhenKeys[whenUtsusemi_]);
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
    // Blink up now (K1), or the preview's cast -> active -> hit -> off cycle.
    auto ShadowModule::HasBlink(Context& ctx) const -> bool
    {
        if (blinkPreview_)
            return previewBlinkUp_;
        auto* mm     = ctx.core != nullptr ? ctx.core->GetMemoryManager() : nullptr;
        auto* player = mm != nullptr ? mm->GetPlayer() : nullptr;
        const int16_t* b = player != nullptr ? player->GetBuffs() : nullptr;
        for (int i = 0; b != nullptr && i < 32; ++i)
            if (b[i] == kBlinkId)
                return true;
        return false;
    }

    // K1 state machine plus the ripple and preview timing; fills the blink fields of ctx.shadows.
    void ShadowModule::FrameBlink(Context& ctx, float dt, int utsusemi)
    {
        const double now = NowS();
        if (blinkPreview_) // 6 s loop: cast at 0, hit at 2.5, wears off at 4
        {
            const double t   = std::fmod(now - blinkPreviewAt_, 6.0);
            const bool wasUp = previewBlinkUp_;
            previewBlinkUp_  = t < 4.0;
            if (wasUp && t >= 2.5 && t - dt < 2.5)
                rippleT_ = 0.0f;
        }
        const bool up = HasBlink(ctx);
        if (up && !blinkPrev_)
            phase_ = (blinkFlashOnCast_ && now - zoneAt_ > kZoneSuppress) ? Blink::FlashIn : Blink::Active, phaseT_ = 0.0f;
        if (!up && blinkPrev_)
            phase_ = Blink::FadeOut, phaseT_ = 0.0f;
        blinkPrev_ = up;
        phaseT_ += dt, rippleT_ += dt;
        if (phase_ == Blink::FlashIn && phaseT_ > kFlashIn)
            phase_ = Blink::Active;
        if (phase_ == Blink::FadeOut && phaseT_ > blinkFadeOut_)
            phase_ = Blink::Off;
        blinkClock_ += dt * blinkSpeed_;
        if (blinkClock_ > 10000.0f)
            blinkClock_ = std::fmod(blinkClock_, 6.2831853f);

        auto& s = ctx.shadows;
        s.blinkA = 0.0f;
        if (!blinkOn_ || phase_ == Blink::Off || (utsusemi > 0 && whenUtsusemi_ == 1))
            return;
        const float dim  = utsusemi > 0 && whenUtsusemi_ == 0 ? 0.5f : 1.0f;
        const float cast = phase_ == Blink::FlashIn ? 1.0f - std::min(1.0f, phaseT_ / kFlashIn) : 0.0f;
        s.blinkA         = blinkOpacity_ * dim;
        s.blinkCondense  = cast;
        s.blinkFlash     = cast;
        s.blinkDissipate = phase_ == Blink::FadeOut ? std::min(1.0f, phaseT_ / blinkFadeOut_) : 0.0f;
        s.blinkRipple    = blinkRipple_ && rippleT_ < kRippleTime ? rippleT_ / kRippleTime : -1.0f;
        s.blinkTime      = blinkClock_;
        s.blinkMotes     = blinkMotes_;
        s.blinkSize      = blinkSize_;
        s.blinkColor     = kColorValues[blinkColor_];
        s.blinkAccent    = hex(0xf3f0e6);
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
        FrameBlink(ctx, dt, n);

        auto& s               = ctx.shadows;
        const bool utsuOn     = style_ != 0 && (n > 0 || outT_ < outTime_);
        if ((!utsuOn && s.blinkA <= 0.0f) || !ctx.camera.Fresh())
            return;
        auto* mm    = ctx.core != nullptr ? ctx.core->GetMemoryManager() : nullptr;
        auto* ent   = mm != nullptr ? mm->GetEntity() : nullptr;
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (ent == nullptr || party == nullptr)
            return;
        const uint32_t me = party->GetMemberTargetIndex(0);
        if (me == 0 || me >= 2304 || ent->GetRawEntity(me) == nullptr || (ent->GetRenderFlags0(me) & 0x4200u) != 0x200u)
            return; // not drawn by the game (zoning, cutscene)
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
        s.want      = true;
        s.style     = utsuOn ? style_ : 0;
        s.time      = spin_;
        s.count     = n;
        s.maxShown  = maxShown_;
        s.lostIdx   = lostT_ < lostTime_ ? lostIdx_ : -1;
        s.lostA     = 1.0f - std::min(1.0f, lostT_ / lostTime_);
        s.inA       = std::min(1.0f, inT_ / 0.25f);
        const float o = n == 0 ? std::min(1.0f, outT_ / outTime_) : 0.0f;
        s.outA      = 1.0f - o;
        s.outScale  = 1.0f + 0.12f * Ease(o);
        s.color     = kColorValues[l.color];
        s.size      = l.size;
        s.opacity   = l.opacity;
        s.glow      = l.glow;
        s.depthMode = depthMode_;
        s.crossfade = crossfade_;
    }

    // K2: a shadow-absorb message on you while only Blink is up plays the Blink hit ripple (with Utsusemi up
    // the game uses Utsusemi first, and its count drop drives that animation instead).
    void ShadowModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id == 0x00A)
        {
            zoneAt_ = NowS(); // zoning re-sends the buffs: don't replay the cast flash
            return;
        }
        if (id != 0x028 || data == nullptr || !blinkOn_ || !blinkRipple_ || phase_ == Blink::Off || phase_ == Blink::FadeOut)
            return;
        auto* party = ctx.core != nullptr && ctx.core->GetMemoryManager() != nullptr ? ctx.core->GetMemoryManager()->GetParty() : nullptr;
        const uint32_t me = party != nullptr ? party->GetMemberServerId(0) : 0;
        if (me == 0 || prev_ > 0)
            return;
        const uint32_t targets = Bits(data, size, 72, 6);
        uint32_t pos = 150;
        for (uint32_t t = 0; t < targets && pos / 8 < size; ++t)
        {
            const uint32_t target = Bits(data, size, pos, 32);
            pos += 32;
            const uint32_t actions = Bits(data, size, pos, 4);
            pos += 4;
            for (uint32_t a = 0; a < actions && pos / 8 < size; ++a)
            {
                pos += 5 + 12 + 7 + 3 + 17;
                const uint32_t msg = Bits(data, size, pos, 10);
                pos += 10 + 31;
                if (target == me && msg == kAbsorbMsg)
                    rippleT_ = 0.0f;
                if (Bits(data, size, pos++, 1) != 0) // additional effect
                    pos += 10 + 17 + 10;
                if (Bits(data, size, pos++, 1) != 0) // spikes
                    pos += 10 + 14 + 10;
            }
        }
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
            ch |= g.Toggle("Hide in first person", "Don't draw shadows or Blink while the camera is in your character's head.", hideFirstPerson_);
            break;
        }
        case 3: // Blink
        {
            ch |= g.Toggle("Shimmer", "Blink as a shimmer of motes around you: they condense when you cast it, ripple when it absorbs a hit and dissipate when it wears off.", blinkOn_);
            ch |= g.Choice("Colour", "Colour of the motes and veil.", blinkColor_, kColorNames, kColors);
            ch |= g.Slider("Opacity", "How strongly it draws.", blinkOpacity_, 0.0f, 1.0f, "%.2f");
            ch |= g.Slider("Size", "Size around your character.", blinkSize_, 0.5f, 2.0f, "%.2f");
            ch |= g.Slider("Motes", "Number of motes.", blinkMotes_, 50, 300);
            ch |= g.Slider("Speed", "Drift and twinkle speed.", blinkSpeed_, 0.0f, 3.0f, "%.2f");
            ch |= g.Toggle("Flash on cast", "Motes condense onto you with a soft flash when Blink goes up.", blinkFlashOnCast_);
            ch |= g.Toggle("Hit ripple", "The motes push outward and brighten when Blink absorbs a hit.", blinkRipple_);
            ch |= g.Slider("Fade out", "How long Blink takes to dissipate when it wears off (seconds).", blinkFadeOut_, 0.2f, 3.0f, "%.2f");
            if (g.Toggle("Preview", "Cycle cast -> active -> hit -> wear-off every 6 s so you can see it outside combat.", blinkPreview_))
                blinkPreviewAt_ = NowS(), previewBlinkUp_ = false;
            break;
        }
        case 4: // Layering
        {
            ch |= g.Choice("Draw over character",
                "Always on top: everything draws over your character. Depth split: the parts in front of you draw over your character and the "
                "parts behind are hidden by it. Fully depth-tested: the 3D scene decides everything.",
                depthMode_, kDepthNames, 3);
            if (depthMode_ == 1)
                ch |= g.Slider("Crossfade", "Width of the blend at your sides, as a share of the effect's radius.", crossfade_, 0.02f, 0.6f, "%.2f");
            ch |= g.Choice("Blink with Utsusemi", "When both are up: dim the Blink shimmer to half, hide it, or show both in full.", whenUtsusemi_, kWhenNames, 3);
            break;
        }
        }
        if (ch)
            ctx.MarkDirty();
    }

    auto ShadowModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        const std::string a1  = args.size() > 1 ? ui::Lower(args[1]) : std::string();
        if (sub == "style" && !a1.empty())
        {
            const int i = a1 == "off" ? 0 : KeyIndex(a1, kKeys, kStyles, -1);
            if (i < 0)
            {
                ctx.Print("FarplaneUI shadows: styles are none, wisps, diamond, runering, smoke, pips.");
                return true;
            }
            style_ = i;
            ctx.MarkDirty();
            ctx.Print(std::string("FarplaneUI shadows: ") + kNames[i] + ".");
            return true;
        }
        if (sub == "depth" || sub == "over")
        {
            int i = KeyIndex(a1, kDepthKeys, 3, -1);
            if (sub == "over")
                i = a1 == "off" ? 1 : 0; // older command: on = always on top, off = depth split
            if (i < 0)
            {
                ctx.Print("FarplaneUI shadows: /fpui shadows depth ontop|split|depth");
                return true;
            }
            depthMode_ = i;
            ctx.MarkDirty();
            ctx.Print(std::string("FarplaneUI shadows: draw over character = ") + kDepthNames[i] + ".");
            return true;
        }
        if (sub == "blink")
        {
            if (a1 == "preview")
            {
                blinkPreview_ = !blinkPreview_, blinkPreviewAt_ = NowS(), previewBlinkUp_ = false;
                ctx.Print(blinkPreview_ ? "FarplaneUI Blink: preview on (/fpui shadows blink preview again to stop)." : "FarplaneUI Blink: preview off.");
                return true;
            }
            blinkOn_ = a1 == "on" ? true : a1 == "off" ? false : !blinkOn_;
            ctx.MarkDirty();
            ctx.Print(blinkOn_ ? "FarplaneUI Blink: shimmer on." : "FarplaneUI Blink: shimmer off.");
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
        ctx.Print(std::string("FarplaneUI shadows: ") + kNames[style_] + ", depth " + kDepthKeys[depthMode_] + ", Blink " + (blinkOn_ ? "on" : "off") +
                  ". /fpui shadows style <none|wisps|diamond|runering|smoke|pips> | depth <ontop|split|depth> | blink [on|off|preview] | preview <0-4>.");
        return true;
    }
} // namespace farplaneui
