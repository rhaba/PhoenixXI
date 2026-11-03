/**
 * Farplane UI - debuffs on your target, as a row of status icons with their time left just above the target bar.
 *
 * The client is never told which effects a monster has, so they are tracked from combat messages, the way the
 * enemybar addon does it (handlers/debuffhandler.lua, GPL-3.0, from mousseng's XITools):
 *   0x028 action   a "status on" message on a target adds the status named by its param (or the action's own one),
 *                  for the action's duration from data/mob_debuffs.hpp; Dia / Bio / helix land with their damage;
 *                  additional effects (procs) add the status they name; a death message clears the target
 *   0x029 message  a "wears off / no longer" message removes that status; a death message clears the target
 *   timers         an effect without a wear-off message goes when its time is up; zoning clears everything
 * Durations are the base ones (no gear, merit or job point bonuses), so a timer can run a little long or short.
 */
#include "common.hpp"
#include "../data/mob_debuffs.hpp"

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
        auto U32(const uint8_t* d, uint32_t size, uint32_t o) -> uint32_t
        {
            return o + 4 <= size ? static_cast<uint32_t>(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<uint32_t>(d[o + 3]) << 24)) : 0;
        }
        auto U16(const uint8_t* d, uint32_t size, uint32_t o) -> uint32_t { return o + 2 <= size ? static_cast<uint32_t>(d[o] | (d[o + 1] << 8)) : 0; }
        template <size_t N>
        auto In(uint32_t m, const uint16_t (&set)[N]) -> bool
        {
            for (const uint16_t s : set)
                if (s == m)
                    return true;
            return false;
        }
        // Message ids (enemybar's lists).
        constexpr uint16_t kStatusOn[]  = {127, 160, 164, 166, 186, 194, 203, 205, 230, 236, 237, 266, 267, 268, 269, 271, 272, 277, 278, 279, 280, 319, 320,
            375, 412, 645, 754, 755, 804};
        constexpr uint16_t kStatusOff[] = {64, 159, 168, 204, 206, 321, 322, 341, 342, 343, 344, 350, 378, 531, 647, 805, 806};
        constexpr uint16_t kDeath[]     = {6, 20, 97, 113, 406, 605, 646};
        constexpr uint16_t kSpellDmg[]  = {2, 252, 264, 265};

        auto FindAction(uint32_t cat, uint32_t id) -> const mobfx::ActionDebuff*
        {
            for (const auto& a : mobfx::kActions)
                if (a.category == cat && a.action == id)
                    return &a;
            return nullptr;
        }
        // An effect without a known source: the longest duration any action gives it, else its proc duration, else 60 s.
        auto FallbackSeconds(uint32_t buff) -> uint32_t
        {
            uint32_t best = 0;
            for (const auto& a : mobfx::kActions)
                if (a.buff == buff)
                    best = std::max<uint32_t>(best, a.seconds);
            if (best == 0)
                for (const auto& e : mobfx::kAdditional)
                    if (e.buff == buff)
                        best = e.seconds;
            return best != 0 ? best : 60;
        }
        auto Short(double s) -> std::string
        {
            char b[16]{};
            const int t = static_cast<int>(std::ceil(std::max(0.0, s)));
            if (t >= 60)
                ::sprintf_s(b, "%dm", (t + 59) / 60);
            else
                ::sprintf_s(b, "%d", t);
            return b;
        }
    } // namespace

    void MobDebuffModule::Add(uint32_t target, uint32_t buff, double seconds)
    {
        if (target == 0 || buff == 0 || buff >= 0x400 || buff == 255)
            return;
        fx_[target][static_cast<uint16_t>(buff)] = NowS() + seconds;
    }

    void MobDebuffModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        (void)ctx;
        if (data == nullptr)
            return;
        if (id == 0x00A) // zoned: nothing carries over
        {
            fx_.clear();
            return;
        }
        if (id == 0x029) // battle message: wear-off / death
        {
            const uint32_t target = U32(data, size, 0x08), param = U32(data, size, 0x0C), msg = U16(data, size, 0x18) & 0x7FFF;
            if (In(msg, kDeath))
                fx_.erase(target);
            else if (In(msg, kStatusOff))
                if (auto it = fx_.find(target); it != fx_.end())
                    it->second.erase(static_cast<uint16_t>(param));
            return;
        }
        if (id != 0x028)
            return;
        const uint32_t category = Bits(data, size, 82, 4), action = Bits(data, size, 86, 16);
        const auto* known       = FindAction(category, action);
        const uint32_t targets  = Bits(data, size, 72, 6);
        uint32_t pos = 150;
        for (uint32_t t = 0; t < targets && pos / 8 < size; ++t)
        {
            const uint32_t target = Bits(data, size, pos, 32);
            pos += 32;
            const uint32_t actions = Bits(data, size, pos, 4);
            pos += 4;
            for (uint32_t a = 0; a < actions && pos / 8 < size; ++a)
            {
                pos += 5 + 12 + 7 + 3;
                const uint32_t param = Bits(data, size, pos, 17);
                pos += 17;
                const uint32_t msg = Bits(data, size, pos, 10);
                pos += 10 + 31;
                if (In(msg, kDeath))
                    fx_.erase(target);
                else if (In(msg, kStatusOn))
                {
                    const uint32_t buff = known != nullptr && known->buff != 0 ? known->buff : param;
                    Add(target, buff, known != nullptr ? known->seconds : FallbackSeconds(buff));
                    if (known != nullptr && known->clears != 0)
                        if (auto it = fx_.find(target); it != fx_.end())
                            it->second.erase(known->clears);
                }
                else if (known != nullptr && known->onDamage != 0 && In(msg, kSpellDmg)) // Dia / Bio / helix land with their damage
                {
                    Add(target, known->buff, known->seconds);
                    if (known->clears != 0)
                        if (auto it = fx_.find(target); it != fx_.end())
                            it->second.erase(known->clears);
                }
                if (Bits(data, size, pos++, 1) != 0) // additional effect
                {
                    pos += 10;
                    const uint32_t ap = Bits(data, size, pos, 17);
                    pos += 17;
                    const uint32_t am = Bits(data, size, pos, 10);
                    pos += 10;
                    if (am == 160 || am == 164) // "<target> is <status>." from a proc
                        Add(target, ap, FallbackSeconds(ap));
                }
                if (Bits(data, size, pos++, 1) != 0) // spikes
                    pos += 10 + 14 + 10;
            }
        }
    }

    void MobDebuffModule::Frame(Context& ctx)
    {
        (void)ctx;
        const double now = NowS();
        if (now - sweptAt_ < 1.0)
            return;
        sweptAt_ = now;
        for (auto it = fx_.begin(); it != fx_.end();)
        {
            for (auto e = it->second.begin(); e != it->second.end();)
                e = e->second <= now ? it->second.erase(e) : std::next(e);
            it = it->second.empty() ? fx_.erase(it) : std::next(it);
        }
    }

    void MobDebuffModule::Render(Context& ctx, IGuiManager* gui)
    {
        const Rect& bar = ctx.targetBarRect;
        if (!show_ || !bar.valid())
            return;
        auto* mm  = ctx.core->GetMemoryManager();
        auto* ent = mm != nullptr ? mm->GetEntity() : nullptr;
        auto* tgt = mm != nullptr ? mm->GetTarget() : nullptr;
        if (ent == nullptr || tgt == nullptr)
            return;
        const uint32_t idx = tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u);
        if (idx == 0 || idx >= 2304 || ent->GetRawEntity(idx) == nullptr || (ent->GetSpawnFlags(idx) & 0x10u) == 0)
            return; // monsters only
        const auto it = fx_.find(ent->GetServerId(idx));
        if (it == fx_.end() || it->second.empty())
            return;
        const double now  = NowS();
        const float scale = ctx.layout.screenH / 1080.0f;
        const float size = std::round(20.0f * scale), gap = std::round(3.0f * scale), textH = std::max(10.0f, 11.0f * scale);
        const float rowH = size + textH + 2.0f;
        const float x0 = bar.x + 6.0f * scale, y0 = bar.y - rowH - 4.0f * scale; // just above the target bar's top line
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_mobfx", nullptr, f))
        {
            gui->PushFont(nullptr, textH);
            float x = x0;
            for (const auto& [buff, until] : it->second)
            {
                if (x + size > bar.x + bar.w)
                    break; // one line, as wide as the target bar
                if (IDirect3DTexture8* ic = mod::StatusIcon(ctx, buff); ic != nullptr)
                    ui::ImageAt(gui, ic, ImVec2(x, y0), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                const double left = until - now;
                const std::string t = Short(left);
                const float tw = gui->CalcTextSize(t.c_str()).x;
                const ImVec4 col = left < 10.0 ? ToVec4(art::gold_hi) : ImVec4(0.96f, 0.95f, 0.9f, 1.0f);
                gui->SetCursorScreenPos(ImVec2(x + (size - tw) * 0.5f + 1, y0 + size + 1));
                gui->TextColored(ImVec4(0, 0, 0, 0.85f), "%s", t.c_str());
                gui->SetCursorScreenPos(ImVec2(x + (size - tw) * 0.5f, y0 + size));
                gui->TextColored(col, "%s", t.c_str());
                const ImVec2 m = gui->GetIO().MousePos;
                if (m.x >= x && m.y >= y0 && m.x < x + size && m.y < y0 + size)
                    gui->SetTooltip("%s  (%s)", mod::BuffName(ctx, buff).c_str(), t.c_str());
                x += size + gap;
            }
            gui->PopFont();
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void MobDebuffModule::Load(IConfigurationManager* cfg, const char* alias) { show_ = cfg->GetBool(alias, "mobfx", "show", show_); }
    void MobDebuffModule::Save(IConfigurationManager* cfg, const char* alias) const { ui::SetB(cfg, alias, "mobfx", "show", show_); }
    void MobDebuffModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        if (g.Toggle("Enabled", "Debuffs on your target as status icons with their time left, just above the target bar. Tracked from combat "
                                "messages (base durations, so a timer can be a little off).", show_))
            ctx.MarkDirty();
    }
    auto MobDebuffModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "on" || sub == "off")
            show_ = sub == "on", ctx.MarkDirty();
        else if (sub == "clear")
            fx_.clear();
        ctx.Print(std::string("Target debuffs: ") + (show_ ? "on" : "off") + ". /fpui mobfx on|off | clear");
        return true;
    }
} // namespace farplaneui
