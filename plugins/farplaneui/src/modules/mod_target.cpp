/**
 * Farplane UI - target module: ground ring + facing arrow under the target, HP bar near it.
 *
 * Position/heading/HP%/model size: IEntity (index from ITarget::GetTargetIndex(0), or the
 * sub-target when it is active). World -> screen: the last perspective VIEW/PROJECTION pair the
 * game set during its 3D draw calls (captured by the host in the draw hooks) and the viewport.
 * FFXI world axes in D3D space are assumed to be (X, height, Y) with height = Ashita's Z
 * times [target] heightSign (default 1); flip with /fpui target flip if the ring floats.
 * ImDrawList is not exposed by IGuiManager, so the ring is drawn as small ImGui image dots.
 */
#include "common.hpp"
#include "../ember.hpp"
#include "../mobdb.hpp"

#include <chrono>
#include <cstring>

#include <cmath>
#include <cstdlib>

namespace farplaneui
{
    namespace
    {
        struct Clip { float x, y, z, w; };
        auto ToClip(const Camera& cam, float x, float y, float z) -> Clip
        {
            const D3DMATRIX& v = cam.view;
            const D3DMATRIX& p = cam.proj;
            const float vx = x * v._11 + y * v._21 + z * v._31 + v._41;
            const float vy = x * v._12 + y * v._22 + z * v._32 + v._42;
            const float vz = x * v._13 + y * v._23 + z * v._33 + v._43;
            const float vw = x * v._14 + y * v._24 + z * v._34 + v._44;
            Clip c{};
            c.x = vx * p._11 + vy * p._21 + vz * p._31 + vw * p._41;
            c.y = vx * p._12 + vy * p._22 + vz * p._32 + vw * p._42;
            c.z = vx * p._13 + vy * p._23 + vz * p._33 + vw * p._43;
            c.w = vx * p._14 + vy * p._24 + vz * p._34 + vw * p._44;
            return c;
        }
        // Screen size the overlay draws in (ImGui coords); set each frame by Render().
        float g_sw = 1920.0f, g_sh = 1080.0f;
        // Works for normal, reversed and infinite projections: only w > 0 (in front) is required,
        // depth is ignored. NDC is mapped to the ImGui screen (backbuffer may be scaled).
        auto Project(const Camera& cam, float x, float y, float z, ImVec2& out) -> bool
        {
            const Clip c = ToClip(cam, x, y, z);
            if (c.w <= 0.001f)
                return false;
            const float nx = c.x / c.w, ny = c.y / c.w;
            if (std::abs(nx) > 4.0f || std::abs(ny) > 4.0f)
                return false;
            out.x = (nx + 1.0f) * 0.5f * g_sw;
            out.y = (1.0f - ny) * 0.5f * g_sh;
            return true;
        }
        // Ashita entity (X, Y, Z=height) -> D3D world candidates; auto-detected per target.
        void ToWorld(int mode, float ax, float ay, float az, float& wx, float& wy, float& wz)
        {
            switch (mode)
            {
            default:
            case 0: wx = ax; wy = az;  wz = ay;  break; // (x, z, y)
            case 1: wx = ax; wy = -az; wz = ay;  break; // (x, -z, y)
            case 2: wx = ax; wy = az;  wz = -ay; break;
            case 3: wx = ax; wy = -az; wz = -ay; break;
            case 4: wx = ax; wy = ay;  wz = az;  break; // (x, y, z)
            case 5: wx = ax; wy = ay;  wz = -az; break;
            }
        }
        constexpr int kModes = 6;
        auto ProjectEnt(const Camera& cam, int mode, float ax, float ay, float az, ImVec2& out) -> bool
        {
            float wx, wy, wz;
            ToWorld(mode, ax, ay, az, wx, wy, wz);
            return Project(cam, wx, wy, wz, out);
        }
        auto OnScreen(const ImVec2& s) -> bool { return s.x >= 0 && s.y >= 0 && s.x <= g_sw && s.y <= g_sh; }
        // ITarget slot 0 is the main target; while a sub-target cursor (<st>) is active the
        // selection lives in slot 1 (the common addon pattern GetTargetIndex(GetIsSubTargetActive())).
        auto CurrentTargetIndex(IAshitaCore* core) -> uint32_t
        {
            auto* mm  = core != nullptr ? core->GetMemoryManager() : nullptr;
            auto* tgt = mm != nullptr ? mm->GetTarget() : nullptr;
            auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
            if (tgt == nullptr || ent == nullptr)
                return 0;
            const uint32_t idx = tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u);
            if (idx == 0 || idx >= 2304 || ent->GetRawEntity(idx) == nullptr)
                return 0;
            if (!mod::ShowObjects() && mod::IsObject(ent, idx))
                return 0; // doors, counters, home points...
            return idx;
        }
        auto NowS(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
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
        // Claim colours (WIRING.md T4).
        constexpr Rgba kUnclaimed{0xe6 / 255.0f, 0xb4 / 255.0f, 0x79 / 255.0f, 1}, kYours{0xe8 / 255.0f, 0x60 / 255.0f, 0x3a / 255.0f, 1};
        constexpr Rgba kParty{0xb4 / 255.0f, 0x8c / 255.0f, 1.0f, 1}, kOther{0x8a / 255.0f, 0x80 / 255.0f, 0x70 / 255.0f, 1};
        constexpr Rgba kPassive{0xbf / 255.0f, 0xee / 255.0f, 0xf0 / 255.0f, 1};
        constexpr float kRangeMelee = 3.0f, kRangeSpell = 20.4f, kPulseDur = 3.0f;
        // Claim state: yours / party or alliance / another group; unclaimed mobs are "passive" until
        // they engage or take damage (the client doesn't say whether a mob aggros).
        auto ClaimColor(IAshitaCore* core, uint32_t idx, float& alphaMul) -> Rgba
        {
            alphaMul    = 1.0f;
            auto* ent   = core->GetMemoryManager()->GetEntity();
            auto* party = core->GetMemoryManager()->GetParty();
            if ((ent->GetSpawnFlags(idx) & 0x10u) == 0)
                return kPassive; // player, NPC, pet
            const uint32_t claim = ent->GetClaimStatus(idx);
            if (claim == 0)
                return ent->GetStatus(idx) == 1 || ent->GetHPPercent(idx) < 100 ? kUnclaimed : kPassive;
            const auto same = [&](uint32_t id) { return id != 0 && (id == claim || (id & 0xFFFF) == (claim & 0xFFFF)); };
            if (same(party->GetMemberServerId(0)))
                return kYours;
            for (uint32_t i = 1; i < 18; ++i)
                if (party->GetMemberIsActive(i) != 0 && same(party->GetMemberServerId(i)))
                    return kParty;
            alphaMul = 0.6f;
            return kOther;
        }
    } // namespace

    // TP move (category 7) / cast start (8) by the target: pulse; the action finishing (11 / 4) ends it.
    void TargetModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr)
            return;
        const uint32_t idx = CurrentTargetIndex(ctx.core);
        auto* ent = ctx.core->GetMemoryManager()->GetEntity();
        if (idx == 0 || ent == nullptr)
            return;
        const uint32_t actor = Bits(data, size, 40, 32), cat = Bits(data, size, 82, 4);
        if (actor != ent->GetServerId(idx))
            return;
        if (cat == 7 || cat == 8)
        {
            if (pulseActor_ != actor || NowS() - pulseStart_ > kPulseDur)
                pulseLabel_.clear();
            pulseStart_ = NowS(), pulseActor_ = actor;
        }
        else if ((cat == 11 || cat == 4) && pulseActor_ == actor)
            pulseStart_ = -100;
    }
    // "<mob> readies <move>." / "<mob> starts casting <spell>." -> the label under the pulsing ring.
    auto TargetModule::IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool
    {
        (void)mode, (void)injected;
        const uint32_t idx = CurrentTargetIndex(ctx.core);
        auto* ent = ctx.core->GetMemoryManager()->GetEntity();
        if (message == nullptr || idx == 0 || ent == nullptr || ent->GetName(idx) == nullptr)
            return false;
        std::string line;
        for (const char* p = message; *p != '\0'; ++p)
        {
            const auto c = static_cast<unsigned char>(*p);
            if (c == 0x1E || c == 0x1F || c == 0xEF || c == 0x7F)
            {
                if (p[1] != '\0')
                    ++p; // colour / auto-translate code + its argument byte
                continue;
            }
            if (c >= 0x20 && c < 0x7F)
                line += static_cast<char>(c);
        }
        if (line.find(ent->GetName(idx)) == std::string::npos)
            return false;
        for (const char* verb : {" readies ", " starts casting "})
            if (const size_t at = line.find(verb); at != std::string::npos)
            {
                std::string what = line.substr(at + std::strlen(verb));
                if (const size_t end = what.find_first_of(".!"); end != std::string::npos)
                    what.resize(end);
                pulseLabel_ = what;
                pulseStart_ = NowS(), pulseActor_ = ent->GetServerId(idx);
            }
        return false;
    }

    void TargetModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "target";
        show_         = cfg->GetBool(alias, S, "show", show_);
        ring_         = cfg->GetBool(alias, S, "ring", ring_);
        bar_          = cfg->GetBool(alias, S, "bar", bar_);
        useModelSize_ = cfg->GetBool(alias, S, "useModelSize", useModelSize_);
        mod::ShowObjects() = cfg->GetBool(alias, S, "objects", mod::ShowObjects());
        ringScale_    = std::clamp(cfg->GetFloat(alias, S, "ringScale", ringScale_), 0.2f, 5.0f);
        axisMode_     = std::clamp(cfg->GetInt32(alias, S, "axis", axisMode_), 0, 5);
        style_        = std::clamp(cfg->GetInt32(alias, S, "style", style_), 0, 2);
        info_         = cfg->GetBool(alias, S, "info", info_);
    }
    void TargetModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "target";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "ring", ring_);
        ui::SetB(cfg, alias, S, "bar", bar_);
        ui::SetB(cfg, alias, S, "useModelSize", useModelSize_);
        ui::SetB(cfg, alias, S, "objects", mod::ShowObjects());
        ui::SetF(cfg, alias, S, "ringScale", ringScale_);
        ui::SetI(cfg, alias, S, "axis", axisMode_);
        ui::SetI(cfg, alias, S, "style", style_);
        ui::SetB(cfg, alias, S, "info", info_);
    }
    auto TargetModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string("toggle") : ui::Lower(args[0]);
        float f = 0;
        if (sub == "toggle" || sub == "on" || sub == "off")
            show_ = sub == "toggle" ? !show_ : sub == "on";
        else if (sub == "ring")
            ring_ = !ring_;
        else if (sub == "bar")
            bar_ = !bar_;
        else if (sub == "axis" && args.size() > 1)
        {
            axisMode_ = std::clamp(std::atoi(args[1].c_str()), 0, 5);
            ctx.Print("target axis set (0 is correct for PhoenixXI).");
        }
        else if (sub == "debug")
        {
            Debug(ctx);
            return true;
        }
        else if (sub == "style" && args.size() > 1)
        {
            const std::string v = ui::Lower(args[1]);
            style_ = v == "arc" ? 1 : v == "classic" ? 2 : 0;
        }
        else if (sub == "info")
        {
            const std::string v = args.size() > 1 ? ui::Lower(args[1]) : std::string();
            info_ = v == "on" ? true : v == "off" ? false : !info_;
        }
        else if (sub == "pulse")
        {
            pulseStart_ = NowS(), pulseLabel_ = "Bomb Toss", pulseActor_ = 0;
            return true;
        }
        else if (sub == "size" && args.size() > 1 && ui::ParseFloat(args[1], f))
            ringScale_ = std::clamp(f, 0.2f, 5.0f);
        else
        {
            ctx.Print("/fpui target [on|off] | ring | bar | style hairline|arc|classic | info on|off | pulse (test) | size <0.2-5> | axis <0-5> | debug");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }
    void TargetModule::Debug(Context& ctx) const
    {
        char buf[256]{};
        const auto& c = ctx.camera;
        ::sprintf_s(buf, "target: show=%d ring=%d bar=%d | camera valid=%d source=%s captures=%u rejected=%u viewport=%lux%lu+%lu,%lu",
            show_ ? 1 : 0, ring_ ? 1 : 0, bar_ ? 1 : 0, c.valid ? 1 : 0, c.source == 1 ? "draw" : (c.source == 2 ? "present" : "none"), c.captures,
            c.rejected, static_cast<unsigned long>(c.viewport.Width), static_cast<unsigned long>(c.viewport.Height),
            static_cast<unsigned long>(c.viewport.X), static_cast<unsigned long>(c.viewport.Y));
        ctx.Print(buf);
        ::sprintf_s(buf, "  frame=%u lastCapture=%u fresh=%d axis=%d jumpSkips=%u | rejected proj=%u view=%u", c.frame, c.lastFrame,
            c.Fresh() ? 1 : 0, axisMode_, skipped_, c.rejProj, c.rejView);
        ctx.Print(buf);
        ::sprintf_s(buf, "  ring3d: hooked=%d want=%d lastDrawn=%d frames ago zfunc=%lu sceneTarget=%p", ctx.ring.hooked ? 1 : 0, ctx.ring.want ? 1 : 0,
            ctx.ring.drawnFrame == 0xFFFFFFFFu ? -1 : static_cast<int>(c.frame - ctx.ring.drawnFrame), static_cast<unsigned long>(c.zFunc), c.sceneTarget);
        ctx.Print(buf);
        ::sprintf_s(buf, "  proj _11=%.3f _22=%.3f _33=%.3f _34=%.3f _43=%.3f _44=%.3f", static_cast<double>(c.proj._11),
            static_cast<double>(c.proj._22), static_cast<double>(c.proj._33), static_cast<double>(c.proj._34), static_cast<double>(c.proj._43),
            static_cast<double>(c.proj._44));
        ctx.Print(buf);
        auto* mm  = ctx.core->GetMemoryManager();
        auto* tgt = mm != nullptr ? mm->GetTarget() : nullptr;
        auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
        if (tgt == nullptr || ent == nullptr)
        {
            ctx.Print("  ITarget/IEntity unavailable.");
            return;
        }
        ::sprintf_s(buf, "  ITarget slot0=%u slot1=%u subActive=%u -> index=%u", tgt->GetTargetIndex(0), tgt->GetTargetIndex(1),
            static_cast<unsigned>(tgt->GetIsSubTargetActive()), CurrentTargetIndex(ctx.core));
        ctx.Print(buf);
        const uint32_t idx = CurrentTargetIndex(ctx.core);
        if (idx == 0)
            return;
        const float x = ent->GetLocalPositionX(idx), y = ent->GetLocalPositionY(idx), z = ent->GetLocalPositionZ(idx);
        g_sw = ctx.layout.screenW; g_sh = ctx.layout.screenH;
        ::sprintf_s(buf, "  view r0=(%.2f %.2f %.2f %.2f) r1=(%.2f %.2f %.2f %.2f)", c.view._11, c.view._12, c.view._13, c.view._14, c.view._21,
            c.view._22, c.view._23, c.view._24);
        ctx.Print(buf);
        ::sprintf_s(buf, "  view r2=(%.2f %.2f %.2f %.2f) r3=(%.2f %.2f %.2f %.2f)", c.view._31, c.view._32, c.view._33, c.view._34, c.view._41,
            c.view._42, c.view._43, c.view._44);
        ctx.Print(buf);
        for (int m = 0; m < kModes; ++m)
        {
            float wx, wy, wz;
            ToWorld(m, x, y, z, wx, wy, wz);
            const Clip k = ToClip(c, wx, wy, wz);
            ImVec2 sc{};
            const bool ok = Project(c, wx, wy, wz, sc);
            ::sprintf_s(buf, "  axis %d clip=(%.2f %.2f %.2f w=%.2f) -> %s (%.0f, %.0f)%s", m, k.x, k.y, k.z, k.w, ok ? "ok" : "FAILED", sc.x, sc.y,
                m == axisMode_ ? " <- active" : "");
            ctx.Print(buf);
        }
        ::sprintf_s(buf, "  entity pos=(%.2f, %.2f, %.2f) yaw=%.2f hp=%u%% size=%.2f axis=%d", static_cast<double>(x),
            static_cast<double>(y), static_cast<double>(z), static_cast<double>(ent->GetLocalPositionYaw(idx)),
            static_cast<unsigned>(ent->GetHPPercent(idx)), static_cast<double>(ent->GetModelSize(idx)), axisMode_);
        ctx.Print(buf);
    }
    void TargetModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Ground ring and HP bar on your target.", show_);
        ch |= g.Toggle("Ground ring", "Ring with a facing arrow under the target, drawn in the 3D scene.", ring_);
        static const char* const kStyles[] = {"Hairline", "HP arc", "Classic band"};
        ch |= g.Toggle("Ring info", "Name, level and detection above the ring; HP, resists / weaknesses and distance below it (mobdb data).", info_);
        ch |= g.Choice("Ring style", "Hairline: thin glowing ring with an HP pill under it. HP arc: the ring itself shows HP. Classic: the original wide band.", style_, kStyles, 3);
        ch |= g.Toggle("HP bar", "Slim HP bar with the percentage under the target.", bar_);
        ch |= g.Toggle("Show on objects", "Also ring / bar doors, counters, home points and other fixed objects (target bar too).", mod::ShowObjects());
        ch |= g.Toggle("Scale with model size", "Size the ring from the target's model size when known.", useModelSize_);
        ch |= g.Slider("Ring scale", "Multiplier on the ring radius.", ringScale_, 0.2f, 5.0f, "%.2f");
        ch |= g.Slider("Axis mode", "World axis mapping (0 is correct for PhoenixXI).", axisMode_, 0, 5);
        if (ch)
            ctx.MarkDirty();
        g.Info("Camera", ctx.camera.valid ? "Captured." : "Not captured yet (needs a 3D frame).");
    }

    // Ring info overlay (incoming/ember_dock4 WIRING.md R1-R6): name pill with aggro dot + level above
    // the ring's far side, detection chips beside it, HP pill under the near side with the notable
    // damage modifiers under it, distance at the ring's right edge. Data: mobdb (shared with the
    // target bar). Anchors are the ring's screen extremes, smoothed so the chips don't jitter.
    void TargetModule::RenderRingInfo(Context& ctx, IGuiManager* gui, uint32_t idx, bool projected, const ImVec2& top, const ImVec2& bot,
        const ImVec2& right, const ImVec2& centre, float dist, float hp, const Rgba& claim, const Rgba& distCol)
    {
        const auto& t   = ember::T();
        const float s   = ctx.layout.screenH / 1080.0f;
        auto* mm        = ctx.core->GetMemoryManager();
        auto* ent       = mm->GetEntity();
        const char* nm  = ent->GetName(idx);
        const bool mob  = (ent->GetSpawnFlags(idx) & 0x10u) != 0;
        std::string install = ctx.core->GetInstallPath();
        while (!install.empty() && (install.back() == '\\' || install.back() == '/'))
            install.pop_back();
        install += "\\";
        MobDb::Shared().Load(ctx.resourceDir + "mobdb\\", mm->GetParty()->GetMemberZone(0));
        const MobInfo* info = mob ? MobDb::Shared().Find(idx, nm) : nullptr;
        const std::string icons = ctx.resourceDir + "mobdb\\icons\\";

        // Smoothed anchors (reset on a new target).
        const double now = NowS();
        const float k    = static_cast<float>(1.0 - std::exp(-std::clamp(now - anchorAt_, 0.0, 0.1) * 18.0));
        anchorAt_        = now;
        auto ease = [&](ImVec2& a, const ImVec2& b) { a = (idx != anchorIdx_) ? b : ImVec2(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k); };
        ImVec2 tTop = top, tBot = bot, tRight = right, tCen = centre;
        if (!projected) // fallback: top-centre, name + HP only
            tCen = ImVec2(ctx.layout.screenW * 0.5f, ctx.layout.screenH * 0.12f), tTop = tCen, tBot = tCen;
        ease(aTop_, tTop), ease(aBot_, tBot), ease(aRight_, tRight), ease(aCen_, tCen);
        anchorIdx_ = idx;

        const float chipH = t.S(18), chipGap = t.S(4);
        auto chipW = [&](const char* label) { return label != nullptr ? chipH + gui->CalcTextSize(label).x + t.S(8) : chipH; };
        auto chip  = [&](const ImVec2& p, const std::string& iconFile, const Rgba& col, const Rgba& bg, const char* label, const char* tip) {
            const float w = chipW(label);
            ember::RoundRect(gui, p, ImVec2(w, chipH), bg, Rgba{col.r, col.g, col.b, 0.67f}, chipH * 0.5f, 1.0f);
            if (IDirect3DTexture8* ic = ctx.textures->File(iconFile); ic != nullptr)
            {
                const float is = chipH * 0.72f;
                ui::ImageAt(gui, ic, ImVec2(p.x + (chipH - is) * 0.5f, p.y + (chipH - is) * 0.5f), ImVec2(is, is), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
            }
            if (label != nullptr)
                ember::Text(gui, ImVec2(p.x + chipH - 1, p.y + (chipH - gui->GetTextLineHeight()) * 0.5f), col, label);
            const ImVec2 m = gui->GetIO().MousePos;
            if (tip != nullptr && m.x >= p.x && m.x < p.x + w && m.y >= p.y && m.y < p.y + chipH)
                gui->SetTooltip("%s", tip);
            return w;
        };
        const Rgba aggroCol = ember::Hex(0xe5482fff), aggroBg = ember::Hex(0x3a1218d7), passiveCol = ember::Hex(0xd8e6f0ff);
        const Rgba resistCol = ember::Hex(0xe5482fff), resistBg = ember::Hex(0x3a1218d7), weakCol = ember::Hex(0x7cc9b6ff), weakBg = ember::Hex(0x0f2a26d7);

        // --- measure the name pill
        gui->PushFont(nullptr, std::max(10.0f, 12.0f * s));
        const std::string name = nm != nullptr ? nm : "";
        const float nameW = gui->CalcTextSize(name.c_str()).x;
        gui->PopFont();
        char lv[24]{};
        if (info != nullptr && info->maxLevel > 0)
        {
            if (info->minLevel == info->maxLevel)
                ::sprintf_s(lv, "Lv.%d", info->maxLevel);
            else
                ::sprintf_s(lv, "Lv.%d-%d", info->minLevel, info->maxLevel);
        }
        gui->PushFont(nullptr, std::max(9.0f, 10.0f * s));
        const float lvW = lv[0] != '\0' ? gui->CalcTextSize(lv).x : 0.0f;
        gui->PopFont();
        const float dot = t.S(6), padL = t.S(10), padR = t.S(11);
        const float pillH = t.S(22), pillW = padL + dot + t.S(6) + nameW + (lvW > 0 ? t.S(10) + lvW : 0.0f) + padR;

        // --- resist / weak chips (notable only, max 4): weak = takes more damage (+N%, green), resist = less (-N%, red)
        struct Res
        {
            int kind, pct;
        };
        std::vector<Res> res;
        if (info != nullptr)
            for (int i = 0; i < MobInfo::kModCount; ++i)
            {
                const int pct = static_cast<int>(std::lround((info->mods[i] - 1.0f) * 100.0f));
                if (std::abs(pct) >= 15)
                    res.push_back({i, pct});
            }
        std::stable_sort(res.begin(), res.end(), [](const Res& a, const Res& b) {
            if ((a.pct < 0) != (b.pct < 0))
                return a.pct < 0; // resists first
            return std::abs(a.pct) > std::abs(b.pct);
        });
        if (res.size() > 4)
            res.resize(4);

        // --- vertical layout: name above the ring, resists just under it (no HP here: the target bar has it);
        // the name flips under the resists if it would leave the top of the screen.
        (void)hp;
        float nameY      = aTop_.y - t.S(18) - pillH;
        const float resY = aBot_.y + t.S(10);
        if (projected && nameY < 4.0f)
            nameY = resY + (res.empty() ? 0.0f : chipH + t.S(6));
        if (!projected)
            nameY = aCen_.y;
        const float cx = std::clamp(aCen_.x, pillW * 0.5f + 4.0f, ctx.layout.screenW - pillW * 0.5f - 4.0f);

        // Name pill: status dot (red = aggressive, pale = passive), name, level right-aligned.
        const ImVec2 np(cx - pillW * 0.5f, nameY);
        ember::RoundRect(gui, np, ImVec2(pillW, pillH), t.glass, t.edge, pillH * 0.5f, 1.0f);
        const bool aggro = info != nullptr && info->aggro;
        ember::RoundRect(gui, ImVec2(np.x + padL, np.y + (pillH - dot) * 0.5f), ImVec2(dot, dot), mob ? (aggro ? aggroCol : passiveCol) : claim,
            Rgba{0, 0, 0, 0}, dot * 0.5f, 0.0f);
        gui->PushFont(nullptr, std::max(10.0f, 12.0f * s));
        ember::Text(gui, ImVec2(np.x + padL + dot + t.S(6), np.y + (pillH - gui->GetTextLineHeight()) * 0.5f), t.ivory, name.c_str());
        gui->PopFont();
        gui->PushFont(nullptr, std::max(9.0f, 10.0f * s));
        if (lv[0] != '\0')
            ember::TextRight(gui, np.x + pillW - padR, np.y + (pillH - gui->GetTextLineHeight()) * 0.5f, t.amber, lv);
        // Detection chips to the right of the name pill.
        if (info != nullptr && projected)
        {
            float x       = np.x + pillW + t.S(8);
            const float y = np.y + (pillH - chipH) * 0.5f;
            const Rgba& col = aggro ? aggroCol : passiveCol;
            const Rgba& bg  = aggro ? aggroBg : t.glass;
            struct D
            {
                bool on;
                const char *file, *tip;
            };
            const D ds[] = {{info->trueSight, "TrueSight.png", "True sight"}, {info->sight && !info->trueSight, "Sight.png", "Detects by sight"},
                {info->sound, "Sound.png", "Detects by sound"}, {info->magic, "Magic.png", "Detects magic"}, {info->scent, "Scent.png", "Detects by scent"},
                {info->blood, "Blood.png", "Detects low HP"}, {info->ja, "JA.png", "Detects job abilities"}, {info->link, "Link.png", "Links"}};
            for (const auto& d : ds)
                if (d.on)
                    x += chip(ImVec2(x, y), icons + d.file, col, bg, nullptr, d.tip) + chipGap;
        }

        // Resist row, centred under the ring.
        if (!res.empty() && projected)
        {
            std::vector<std::string> labels;
            float total = 0;
            for (const auto& r : res)
            {
                char b[12]{};
                ::sprintf_s(b, "%+d%%", r.pct);
                labels.emplace_back(b);
                total += chipW(b) + chipGap;
            }
            total -= chipGap;
            float x = cx - total * 0.5f;
            for (size_t i = 0; i < res.size(); ++i)
            {
                const bool weak = res[i].pct > 0;
                char tip[64]{};
                ::sprintf_s(tip, "%s: takes %d%% damage", MobInfo::kModNames[res[i].kind], 100 + res[i].pct);
                x += chip(ImVec2(x, resY), icons + MobInfo::kModNames[res[i].kind] + ".png", weak ? weakCol : resistCol, weak ? weakBg : resistBg,
                         labels[i].c_str(), tip) + chipGap;
            }
        }

        // Distance at the ring's right edge (the HP-arc style keeps its own tick + pill on the left).
        if (projected && style_ != 1)
        {
            char d[16]{};
            ::sprintf_s(d, "%.1fy", static_cast<double>(dist));
            const ImVec2 ts = gui->CalcTextSize(d);
            const ImVec2 sz(ts.x + t.S(14), chipH);
            const ImVec2 p(std::min(aRight_.x - t.S(8), ctx.layout.screenW - sz.x - 4.0f), aCen_.y - sz.y * 0.5f);
            ember::RoundRect(gui, p, sz, t.glass, t.edge, sz.y * 0.5f, 1.0f);
            ember::TextCenter(gui, p.x + sz.x * 0.5f, p.y + (sz.y - gui->GetTextLineHeight()) * 0.5f, distCol, d);
        }
        gui->PopFont();
        infoBottom_ = std::max(resY + (res.empty() ? 0.0f : chipH), projected ? resY : nameY + pillH);
    }

    void TargetModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_ || ctx.white == nullptr)
            return;
        auto* mm  = ctx.core->GetMemoryManager();
        auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
        const uint32_t idx = CurrentTargetIndex(ctx.core);
        if (ent == nullptr || idx == 0)
            return;
        (void)ctx.camera.valid;

        g_sw = ctx.layout.screenW; g_sh = ctx.layout.screenH;
        const float ax = ent->GetLocalPositionX(idx), ay = ent->GetLocalPositionY(idx), az = ent->GetLocalPositionZ(idx);
        const float yaw = ent->GetLocalPositionYaw(idx);
        float radius = 1.0f * ringScale_; // sane default when model size is unknown (<= 0)
        if (useModelSize_)
        {
            const float ms = ent->GetModelSize(idx);
            if (ms > 0.1f && ms < 30.0f)
                radius = ms * 0.5f * ringScale_;
        }
        if (style_ != 2)
            radius = std::clamp(radius, 0.6f, 8.0f); // huge NMs don't fill the screen, tiny mobs stay readable
        const bool fresh = ctx.camera.Fresh();
        // Only project entities the game is actually rendering (RenderFlags0 & 0x4200 == 0x200, the
        // same visibility test xiuinative uses); otherwise the HP bar uses its fallback position.
        const bool rendered = (ent->GetRenderFlags0(idx) & 0x4200u) == 0x200u;
        int mode = fresh && rendered ? axisMode_ : -1;
        // Jump guard: same target, centre moved > 30% of the screen in one frame -> skip ring this frame.
        if (mode >= 0)
        {
            ImVec2 c0{};
            if (!ProjectEnt(ctx.camera, mode, ax, ay, az, c0))
                mode = -1;
            else
            {
                const bool jump = idx == prevIdx_ && prevPos_.x >= 0 &&
                                  (std::abs(c0.x - prevPos_.x) > g_sw * 0.3f || std::abs(c0.y - prevPos_.y) > g_sh * 0.3f);
                prevPos_ = c0;
                if (jump)
                {
                    ++skipped_;
                    mode = -1;
                }
            }
        }
        else
            prevPos_ = ImVec2(-1, -1);
        prevIdx_ = idx;
        // ring point at ground offset (dx along X, dy along Ashita Y)
        auto P = [&](float dx, float dy, ImVec2& o) { return mode >= 0 && ProjectEnt(ctx.camera, mode, ax + dx, ay + dy, az, o); };
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_target_overlay", nullptr, f))
        {
            ImVec2 s{};
            // Preferred: the host draws a solid ring inside the 3D scene (occluded by the mob and
            // terrain). The dotted 2D ring is only the fallback when that isn't running.
            const bool ring3d = ctx.ring.hooked && ctx.ring.drawnFrame != 0xFFFFFFFFu && ctx.camera.frame - ctx.ring.drawnFrame <= 2;
            // Claim colour, range, HP easing and the pulse feed both the 3D ring and the 2D labels.
            const double now   = NowS();
            const float dt     = static_cast<float>(std::clamp(now - lastFrameAt_, 0.0, 0.1));
            lastFrameAt_       = now;
            float claimAlpha   = 1.0f;
            const Rgba claim   = ClaimColor(ctx.core, idx, claimAlpha);
            const float dist   = std::sqrt(std::max(0.0f, ent->GetDistance(idx)));
            const float rangeA = dist > kRangeSpell ? 0.55f : 1.0f;
            const Rgba tickCol = dist <= kRangeMelee ? claim : dist <= kRangeSpell ? kUnclaimed : ember::T().dim;
            const float hpNow  = static_cast<float>(ent->GetHPPercent(idx)) / 100.0f;
            if (idx != hpIdx_)
                hpIdx_ = idx, hpShown_ = hpGhost_ = hpNow;
            if (hpNow < hpShown_ - 0.001f)
                hpGhost_ = std::max(hpGhost_, hpShown_), ghostAt_ = now; // the drained part flashes ivory
            hpShown_ += (hpNow - hpShown_) * std::min(1.0f, dt * 6.0f);
            const float ghostA = static_cast<float>(std::clamp(1.0 - (now - ghostAt_) / 0.4, 0.0, 1.0));
            if (ghostA <= 0.0f)
                hpGhost_ = hpShown_;
            const float pulseAge = static_cast<float>(now - pulseStart_);
            const bool pulsing   = pulseAge >= 0.0f && pulseAge < kPulseDur;
            // Screen size of the ring: yalms per pixel at the target, so widths stay in screen px.
            float pxWorld = 0.02f;
            {
                ImVec2 a{}, b{};
                if (P(0, 0, a) && P(radius, 0, b))
                {
                    const float d = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
                    if (d > 1.0f)
                        pxWorld = radius / d;
                }
            }
            // Player in the rear 90 degrees (sneak attack zone) and close enough to hit.
            bool rearIn = false;
            if (auto* party = mm->GetParty(); party != nullptr)
            {
                const uint32_t me = party->GetMemberTargetIndex(0);
                const float dx = ent->GetLocalPositionX(me) - ax, dy = ent->GetLocalPositionY(me) - ay;
                const float rx = -std::cos(yaw), ry = std::sin(yaw); // behind the mob (facing = (cos yaw, -sin yaw))
                const float len = std::sqrt(dx * dx + dy * dy);
                rearIn = len > 0.01f && len < radius + kRangeMelee && (dx * rx + dy * ry) / len > std::cos(0.7854f);
            }
            const float scaleRes = ctx.layout.screenH / 1080.0f;
            if (ring_ && mode >= 0)
            {
                auto& r      = ctx.ring;
                r.want       = true;
                r.x          = ax, r.y = az, r.z = ay; // game world (X, height, Y)
                r.radius     = radius;
                r.yaw        = yaw;
                r.style      = style_;
                r.color      = style_ == 2 ? Rgba{0.97f, 0.94f, 0.74f, 1.0f} : claim;
                r.alpha      = claimAlpha * rangeA;
                r.pxWorld    = pxWorld * scaleRes;
                r.rearIn     = rearIn;
                r.hp         = hpShown_;
                r.hpGhost    = hpGhost_;
                r.ghostAlpha = ghostA;
                r.tickColor  = tickCol;
                r.pulseAge   = pulsing ? pulseAge : -1.0f;
            }
            if (ring_ && mode >= 0 && !ring3d)
            {
                constexpr int kDots = 72;
                for (int i = 0; i < kDots; ++i)
                {
                    const float a = static_cast<float>(i) * 6.2831853f / static_cast<float>(kDots);
                    if (P(std::cos(a) * radius, std::sin(a) * radius, s))
                        ui::ImageAt(gui, ctx.white, ImVec2(s.x - 1.5f, s.y - 1.5f), ImVec2(3, 3), ImVec2(0, 0), ImVec2(1, 1),
                            (i % 2) == 0 ? art::gold_hi : art::amber, 0.9f);
                }
                // Facing arrow: filled triangle of dots just outside the ring (yaw: 0 = +X, assumed).
                const float fx = std::cos(yaw), fy = -std::sin(yaw);
                const float px = -fy, py = fx;
                for (int row = 0; row < 8; ++row)
                {
                    const float t     = static_cast<float>(row) / 7.0f;            // 0 = base, 1 = tip
                    const float along = radius * (1.08f + 0.35f * t);
                    const float half  = radius * 0.18f * (1.0f - t);
                    const int n       = std::max(1, static_cast<int>(half / (radius * 0.04f)));
                    for (int k = -n; k <= n; ++k)
                    {
                        const float off = half * static_cast<float>(k) / static_cast<float>(n);
                        if (P(fx * along + px * off, fy * along + py * off, s))
                            ui::ImageAt(gui, ctx.white, ImVec2(s.x - 2, s.y - 2), ImVec2(4, 4), ImVec2(0, 0), ImVec2(1, 1), art::gold_hi);
                    }
                }
            }
            // HP bar: under the projected target, or at a fixed top-centre anchor if projection fails.
            bool anchored = P(0, 0, s) && OnScreen(s);
            if (!anchored)
                s = ImVec2(ctx.layout.screenW * 0.5f, ctx.layout.screenH * 0.10f);
            const auto& et = ember::T();
            // The ring's screen extremes: near side (lowest), far side (highest), right edge.
            ImVec2 nearPt{}, farPt{}, rightPt{}, centrePt{};
            bool hasNear = false;
            for (int k = 0; k < 32 && mode >= 0; ++k)
            {
                const float a = static_cast<float>(k) * 6.28318531f / 32.0f;
                ImVec2 q{};
                if (!P(std::cos(a) * radius, std::sin(a) * radius, q) || !OnScreen(q))
                    continue;
                if (!hasNear)
                    nearPt = farPt = rightPt = q, hasNear = true;
                if (q.y > nearPt.y)
                    nearPt = q;
                if (q.y < farPt.y)
                    farPt = q;
                if (q.x > rightPt.x)
                    rightPt = q;
            }
            const bool hasCentre = hasNear && P(0, 0, centrePt);
            const bool showInfo  = info_ && style_ != 2;
            if (showInfo)
                RenderRingInfo(ctx, gui, idx, hasCentre, farPt, nearPt, rightPt, centrePt, dist, hpShown_, claim, tickCol);
            if (bar_ && style_ == 0 && hasNear && !showInfo)
            {
                // Hairline: HP pill under the ring with the percentage below it.
                const float pw = 120.0f * scaleRes, ph = 7.0f * scaleRes;
                const ImVec2 pp(nearPt.x - pw * 0.5f, nearPt.y + 16.0f * scaleRes);
                ember::Bar(ctx, gui, pp, ImVec2(pw, ph), hpShown_, claim);
                char label[16]{};
                ::sprintf_s(label, "%d%%", static_cast<int>(ent->GetHPPercent(idx)));
                gui->PushFont(nullptr, std::max(10.0f, 12.0f * scaleRes));
                ember::TextCenter(gui, nearPt.x + 1, pp.y + ph + 3, Rgba{0, 0, 0, 0.8f}, label);
                ember::TextCenter(gui, nearPt.x, pp.y + ph + 2, et.ivory, label);
                gui->PopFont();
            }
            if (style_ == 1 && mode >= 0)
            {
                gui->PushFont(nullptr, std::max(9.0f, 11.0f * scaleRes));
                // NN% above the end of the HP arc.
                const float a0 = -yaw + ctx.ring.notch, span = 6.28318531f - 2 * ctx.ring.notch, ea = a0 + span * hpShown_;
                ImVec2 cap{};
                if (P(std::cos(ea) * radius, std::sin(ea) * radius, cap) && OnScreen(cap)) // ring space (X, Y) = Ashita (X, Y)
                {
                    char label[16]{};
                    ::sprintf_s(label, "%d%%", static_cast<int>(ent->GetHPPercent(idx)));
                    ember::TextCenter(gui, cap.x + 1, cap.y - 14.0f * scaleRes - gui->GetTextLineHeight() + 1, Rgba{0, 0, 0, 0.8f}, label);
                    ember::TextCenter(gui, cap.x, cap.y - 14.0f * scaleRes - gui->GetTextLineHeight(), et.ivory, label);
                }
                // Distance pill past the tick on the left side.
                ImVec2 tick{};
                if (P(-radius * 1.14f, 0, tick) && OnScreen(tick))
                {
                    char d[16]{};
                    ::sprintf_s(d, "%.1fy", static_cast<double>(dist));
                    const ImVec2 ts = gui->CalcTextSize(d);
                    const ImVec2 sz(ts.x + 12.0f * scaleRes, ts.y + 2.0f);
                    const ImVec2 pp(tick.x - 4.0f * scaleRes - sz.x, tick.y - sz.y * 0.5f);
                    ember::RoundRect(gui, pp, sz, et.glass, et.edgeDim, sz.y * 0.5f, 1.0f);
                    ember::TextCenter(gui, pp.x + sz.x * 0.5f, pp.y + 1, tickCol, d);
                }
                gui->PopFont();
            }
            if (pulsing && hasNear && !pulseLabel_.empty())
            {
                gui->PushFont(nullptr, std::max(10.0f, 12.0f * scaleRes));
                const float ly = showInfo ? infoBottom_ + 6.0f * scaleRes : nearPt.y + (style_ == 0 && bar_ ? 44.0f : 20.0f) * scaleRes;
                ember::TextCenter(gui, nearPt.x + 1, ly + 1, Rgba{0, 0, 0, 0.8f}, pulseLabel_.c_str());
                ember::TextCenter(gui, nearPt.x, ly, et.ivory, pulseLabel_.c_str());
                gui->PopFont();
            }
            if (bar_ && style_ == 2)
            {
                // Slim bar with the percentage underneath (no name; the game's target window has it).
                const int pct  = static_cast<int>(ent->GetHPPercent(idx));
                const float hp = static_cast<float>(pct) / 100.0f;
                const float w = 84.0f, bh = 4.0f;
                const ImVec2 pos(s.x - w * 0.5f, s.y + 10.0f);
                const Rgba fill = hp > 0.5f ? Rgba{0.55f, 0.82f, 0.62f, 1.0f} : hp > 0.25f ? art::amber : ctx.palette->danger;
                ui::ImageAt(gui, ctx.white, ImVec2(pos.x - 1, pos.y - 1), ImVec2(w + 2, bh + 2), ImVec2(0, 0), ImVec2(1, 1), Rgba{0.05f, 0.05f, 0.08f, 1.0f}, 0.85f);
                ui::ImageAt(gui, ctx.white, pos, ImVec2(w, bh), ImVec2(0, 0), ImVec2(1, 1), Rgba{0.25f, 0.2f, 0.22f, 1.0f}, 0.9f);
                ui::ImageAt(gui, ctx.white, pos, ImVec2(w * hp, bh), ImVec2(0, 0), ImVec2(1, 1), fill);
                char label[16]{};
                ::sprintf_s(label, "%d%%", pct);
                const ImVec2 ts = gui->CalcTextSize(label);
                const ImVec2 tp(s.x - ts.x * 0.5f, pos.y + bh + 2);
                gui->SetCursorScreenPos(ImVec2(tp.x + 1, tp.y + 1)); // drop shadow
                gui->TextColored(ImVec4(0, 0, 0, 0.8f), "%s", label);
                gui->SetCursorScreenPos(tp);
                gui->TextColored(ImVec4(0.96f, 0.95f, 0.9f, 1.0f), "%s", label);
            }
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }
} // namespace farplaneui
