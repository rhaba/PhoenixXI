/**
 * Farplane UI - party module (anchored bottom-right in the mask; buffs will flow left of it).
 */
#include "common.hpp"
#include "../ember.hpp"

#include <unordered_map>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kJobs[] = {"---", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM", "NIN", "DRG",
            "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN"};
        auto JobName(uint8_t id) -> const char*
        {
            return id < sizeof(kJobs) / sizeof(kJobs[0]) ? kJobs[id] : "???";
        }

        // Bar using the art kit trough + fill tiles when present; ImGui ProgressBar otherwise.
        void ArtBar(Context& ctx, IGuiManager* gui, float frac, const char* fillName, const Rgba& fallback, float h, const char* overlay)
        {
            UINT tw = 0, th = 0;
            IDirect3DTexture8* trough = ctx.Art("bar_trough_9slice", &tw, &th);
            IDirect3DTexture8* fill   = ctx.Art(fillName);
            if (trough == nullptr || fill == nullptr)
            {
                ui::Bar(gui, *ctx.palette, frac, fallback, h, overlay);
                return;
            }
            const ImVec2 pos = gui->GetCursorScreenPos();
            const float w    = gui->GetContentRegionAvail().x;
            frac             = std::clamp(frac, 0.0f, 1.0f);
            ui::NineSlice(gui, trough, tw, th, pos, ImVec2(w, h), 4.0f, 3.0f, Rgba{1, 1, 1, 1});
            if (frac > 0.0f)
                ui::ImageAt(gui, fill, ImVec2(pos.x + 2, pos.y + 2), ImVec2((w - 4) * frac, h - 4), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
            gui->Dummy(ImVec2(w, h));
            if (overlay != nullptr && overlay[0] != '\0')
            {
                const ImVec2 ts = gui->CalcTextSize(overlay);
                const ImVec2 back = gui->GetCursorScreenPos();
                gui->SetCursorScreenPos(ImVec2(pos.x + w - ts.x - 4, pos.y + (h - ts.y) * 0.5f));
                gui->TextColored(ToVec4(art::gold_hi), "%s", overlay);
                gui->SetCursorScreenPos(back);
            }
        }
    } // namespace

    namespace
    {
        auto NowSec(void) -> double { return static_cast<double>(::GetTickCount64()) / 1000.0; }
        auto Fit(IGuiManager* gui, const std::string& text, float maxW) -> std::string
        {
            if (maxW <= 0 || gui->CalcTextSize(text.c_str()).x <= maxW)
                return maxW <= 0 ? std::string() : text;
            std::string t = text;
            while (!t.empty() && gui->CalcTextSize((t + "...").c_str()).x > maxW)
                t.pop_back();
            return t + "...";
        }
    } // namespace

    // Party buffs, read from memory: yourself from IPlayer; others from the party status icons the game
    // shares (icon low byte + 2 high bits per slot from the bit mask), matched by server id.
    auto PartyModule::Buffs(Context& ctx, uint32_t slot, uint32_t serverId) -> std::vector<uint16_t>
    {
        std::vector<uint16_t> out;
        if (slot == 0)
        {
            for (const auto& b : mod::PlayerBuffs(ctx))
                out.push_back(b.id);
            return out;
        }
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        for (uint32_t i = 0; i < 5; ++i)
        {
            if (party->GetStatusIconsServerId(i) != serverId)
                continue;
            const uint8_t* icons = party->GetStatusIcons(i);
            const uint64_t mask  = party->GetStatusIconsBitMask(i);
            for (uint32_t j = 0; icons != nullptr && j < 32; ++j)
            {
                if (icons[j] == 0xFF)
                    continue;
                out.push_back(static_cast<uint16_t>(icons[j] | (((mask >> (2 * j)) & 3u) << 8)));
            }
        }
        return out;
    }

    // Buff icons flow left from the frame's left edge, one line per member row.
    void PartyModule::RenderBuffs(Context& ctx, IGuiManager* gui)
    {
        if (!buffs_ || rowsY_.empty())
            return;
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_party_buffs", nullptr, f))
        {
            // Ember: into the free space between the DPS column and the party (WIRING.md: party.x - 14 leftwards).
            const bool em     = ember::On();
            const auto& t     = ember::T();
            const float size  = em ? t.S(t.buffSize) : 16.0f, step = size + (em ? t.S(t.buffGap) : 2.0f);
            const float room  = em ? panelX_ - t.S(14) - (ctx.layout.dps.x + ctx.layout.dps.w + t.S(t.gap)) : 16 * step;
            const size_t cap  = static_cast<size_t>(std::clamp(std::floor((room + (step - size)) / step), 0.0f, 16.0f));
            for (const auto& r : rowsY_)
            {
                const auto ids = Buffs(ctx, r.slot, r.serverId);
                float x = panelX_ - (em ? t.S(14) : 6.0f) - size;
                for (size_t i = 0; i < ids.size() && i < cap; ++i, x -= step)
                    if (IDirect3DTexture8* ic = mod::StatusIcon(ctx, ids[i]); ic != nullptr)
                        ui::ImageAt(gui, ic, ImVec2(x, r.y + (em ? t.S(6) : 2.0f)), ImVec2(size, size), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
            }
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    // ------------------------------------------------------------------------------------------
    // Ember Dock party (v2, incoming/ember_dock2 WIRING.md B1-B8): six rows; name + job, HP pill with
    // value / percent inside, thin MP and TP bars. Each member's effects run in one line through the
    // buff lane to the left (RenderFxLane). Click a row to target the member.
    // ------------------------------------------------------------------------------------------
    namespace
    {
        // Debuffs: the standard status ids (bad states 1-31, elemental DoTs / stat downs 128-149, and
        // the common enfeebles above). Priorities per WIRING.md B3 - its ids were written from memory;
        // these are the usual FFXI status ids, still to be checked in game.
        auto IsDebuff(uint16_t id) -> bool
        {
            if ((id >= 1 && id <= 31) || (id >= 128 && id <= 149))
                return true;
            switch (id)
            {
            case 156: case 167: case 168: case 174: case 175: case 186: case 192: case 193: case 194: case 223: case 404:
                return true;
            default:
                return false;
            }
        }
        auto Prio(uint16_t id, bool debuff) -> uint8_t
        {
            switch (id)
            {
            case 15: case 7: case 28: case 14: case 17: return 0; // doom, petrify, terror, charm
            case 2: case 19: case 10: return 1;                    // sleep, stun
            case 6: case 29: case 16: return 2;                    // silence, mute, amnesia
            case 4: return 3;                                      // paralyze
            case 33: return 20;                                    // haste
            case 43: return 21;                                    // refresh
            case 40: return 22;                                    // protect
            case 41: return 23;                                    // shell
            case 42: return 30;                                    // regen
            case 116: return 35;                                   // phalanx
            case 37: return 36;                                    // stoneskin
            case 36: return 37;                                    // blink
            case 66: case 444: case 445: case 446: return 38;      // utsusemi
            case 251: return 90;                                   // food
            case 253: case 256: case 268: case 512: return 91;     // signet, sanction, sigil, ionis
            default:
                if (id >= 195 && id <= 222)
                    return 40; // songs
                return debuff ? 10 : 50;
            }
        }
        struct Fx
        {
            uint16_t id;
            bool debuff;
            uint8_t prio;
            double left;  // seconds, < 0 unknown
            double total; // seconds, 0 unknown
            auto Frac(void) const -> float { return total > 0 && left >= 0 ? static_cast<float>(std::clamp(left / total, 0.0, 1.0)) : 1.0f; }
        };
        struct MemberFx
        {
            std::vector<Fx> buffs, debuffs; // each sorted by priority
        };
        void SortFx(std::vector<Fx>& v)
        {
            std::stable_sort(v.begin(), v.end(), [](const Fx& a, const Fx& b) { return a.prio != b.prio ? a.prio < b.prio : a.id < b.id; });
        }
        struct LineFit
        {
            float size;
            int collapsed; // buffs folded into +N
        };
        auto FitLine(int nBuffs, int nDebuffs, float laneW, const ember::Theme& t) -> LineFit
        {
            const int n   = nBuffs + nDebuffs;
            const float g = t.S(t.iconGap);
            for (const float sz : {t.S(t.iconSize), t.S(t.iconCompact)})
                if (n <= static_cast<int>((laneW + g) / (sz + g)))
                    return {sz, 0};
            const float sz = t.S(t.iconCompact), pillW = sz + t.S(t.overflowExtra);
            const int slots = std::max(0, static_cast<int>((laneW - pillW) / (sz + g)));
            const int keep  = std::max(0, slots - nDebuffs); // debuffs always stay
            return {sz, std::max(0, nBuffs - keep)};
        }
        // Demo lines (/fpui fxdemo): 4, 17, 6, 13, 35 and 4 effects, like the mockup.
        auto DemoFx(int member, double now) -> MemberFx
        {
            static const uint16_t kBuffPool[] = {33, 43, 40, 41, 42, 116, 37, 36, 66, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207,
                208, 209, 210, 211, 212, 213, 214, 215, 216, 56, 57, 58, 251, 253};
            static const uint16_t kDebuffPool[] = {15, 3, 4, 6, 13, 5, 2};
            static const int kCounts[6][2] = {{4, 0}, {14, 3}, {5, 1}, {13, 0}, {32, 3}, {3, 1}};
            MemberFx m;
            const int b = kCounts[member % 6][0], d = kCounts[member % 6][1];
            for (int i = 0; i < b; ++i)
            {
                const uint16_t id = kBuffPool[(i + member * 3) % std::size(kBuffPool)];
                const double total = 60.0 + 30.0 * (i % 5), left = total - std::fmod(now + i * 7.0, total);
                m.buffs.push_back({id, false, Prio(id, false), left, total});
            }
            for (int i = 0; i < d; ++i)
            {
                const uint16_t id = kDebuffPool[(i + member) % std::size(kDebuffPool)];
                const double total = 30.0, left = total - std::fmod(now + i * 5.0, total);
                m.debuffs.push_back({id, true, Prio(id, true), left, total});
            }
            SortFx(m.buffs);
            SortFx(m.debuffs);
            return m;
        }
    // One effect: rounded tile (red base + edge for debuffs), the status icon, a countdown line under
    // it; expiring effects (and Doom) pulse with a glow.
    void DrawFx(Context& ctx, IGuiManager* gui, const ImVec2& p, float sz, const Fx& e, double now)
    {
        const auto& t = ember::T();
        const float r = std::max(t.S(3), sz * 0.28f), frac = e.Frac();
        const bool expiring = (e.left >= 0 && (frac < t.expireAt || e.left <= t.expireSec)) || e.id == 15;
        if (expiring)
        {
            Rgba glow = t.expireGlow;
            glow.a *= 0.35f * (0.55f + 0.45f * static_cast<float>(std::sin(now * 6.0)));
            ember::RoundRect(gui, ImVec2(p.x - 2, p.y - 2), ImVec2(sz + 4, sz + 4), glow, Rgba{0, 0, 0, 0}, r + 2, 0.0f);
        }
        Rgba edge = expiring ? Rgba{t.expireGlow.r, t.expireGlow.g, t.expireGlow.b, 1.0f} : e.debuff ? t.debuffEdge : t.buffEdge;
        ember::RoundRect(gui, p, ImVec2(sz, sz), e.debuff ? t.debuffFill : t.buffFill, edge, r, expiring ? 1.5f : 1.0f);
        if (IDirect3DTexture8* ic = mod::StatusIcon(ctx, e.id); ic != nullptr)
        {
            const float in = std::max(1.5f, r * 0.35f);
            ui::ImageAt(gui, ic, ImVec2(p.x + in, p.y + in), ImVec2(sz - 2 * in, sz - 2 * in), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
        }
        const float th = std::max(1.0f, t.S(t.timerH));
        const Rgba tc  = frac < t.timerLowAt ? t.bad : e.debuff ? t.bad : t.amber;
        ember::Bar(ctx, gui, ImVec2(p.x + 1, p.y + sz + t.S(t.timerGap)), ImVec2(sz - 2, th), frac, tc, ember::TrackKind::Faint);
    }
    } // namespace

    void PartyModule::RenderEmber(Context& ctx, IGuiManager* gui)
    {
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        const auto& t = ember::T();
        struct M
        {
            uint32_t slot, serverId;
            std::string name, job;
            uint32_t hp, tp;
            float hpp, mpp;
            bool away;
        };
        std::vector<M> ms;
        const bool demo = NowSec() < testUntil_;
        if (demo)
            ms = {{0, 0, "Retainer", "NIN8/BRD4", 1310, 1200, 0.82f, 0.40f, false}, {9, 0, "Kelz", "NIN7/MNK3", 1182, 600, 0.66f, 0.80f, false},
                {9, 0, "Eddythegreat", "NIN7/WAR3", 1328, 300, 0.75f, 0.55f, false}, {9, 0, "Cypher", "NIN7/WHM3", 1154, 2100, 0.24f, 0.95f, false},
                {9, 0, "Ayame", "SAM7/WAR3", 1402, 2900, 0.91f, 0.0f, false}, {9, 0, "Lyra", "RDM7/BLM3", 988, 100, 0.55f, 0.60f, false}};
        else
        {
            const uint16_t myZone = party->GetMemberZone(0);
            for (uint32_t i = 0; i < 6; ++i)
            {
                const char* name = party->GetMemberName(i);
                if (party->GetMemberIsActive(i) == 0 || name == nullptr || name[0] == '\0')
                    continue;
                char job[32]{};
                const uint8_t mj = party->GetMemberMainJob(i), sj = party->GetMemberSubJob(i);
                if (sj != 0)
                    ::sprintf_s(job, "%s%u/%s%u", JobName(mj), party->GetMemberMainJobLevel(i), JobName(sj), party->GetMemberSubJobLevel(i));
                else
                    ::sprintf_s(job, "%s%u", JobName(mj), party->GetMemberMainJobLevel(i));
                ms.push_back({i, party->GetMemberServerId(i), name, job, party->GetMemberHP(i), party->GetMemberTP(i),
                    static_cast<float>(party->GetMemberHPPercent(i)) / 100.0f, static_cast<float>(party->GetMemberMPPercent(i)) / 100.0f,
                    party->GetMemberZone(i) != myZone});
            }
        }
        rowsY_.clear();
        const float rowH = ctx.layout.party.h / 6.0f;
        auto p = mod::BeginPanel(ctx, gui, "##fpui_party", nullptr, docked_, ctx.layout.party, user_, locked_, 0.0f, false, ImGuiWindowFlags_NoScrollWithMouse);
        if (p.open)
        {
            const ImVec2 o = gui->GetWindowPos();
            const float w = ctx.layout.party.w;
            panelX_ = o.x;
            const float nameW = t.S(t.nameW), bx = o.x + nameW, bw = w - nameW;
            const float hpH = t.S(t.hpH), thin = std::max(2.0f, t.S(t.mpTpH));
            const float block = t.S(t.mpTpY) + thin; // content height of one row
            for (size_t i = 0; i < ms.size() && i < 6; ++i)
            {
                const M& m    = ms[i];
                const float y = o.y + rowH * static_cast<float>(i);
                const float c = y + std::max(0.0f, (rowH - block) * 0.5f); // content top, centred in the row
                rowsY_.push_back({m.slot, m.serverId, y, rowH});
                if (i > 0 && ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, ImVec2(o.x, y), ImVec2(w, 1), ImVec2(0, 0), ImVec2(1, 1), t.rowSep);
                gui->PushFont(nullptr, std::max(11.0f, t.S(13)));
                ember::Text(gui, ImVec2(o.x, c - t.S(2)), m.slot == 0 ? t.amber : t.ivory, Fit(gui, m.name, nameW - t.S(6)).c_str());
                gui->PopFont();
                gui->PushFont(nullptr, std::max(9.0f, t.S(10)));
                ember::Text(gui, ImVec2(o.x, c + t.S(12)), t.dim, m.job.c_str());
                gui->PopFont();
                if (m.away)
                {
                    ember::Text(gui, ImVec2(bx, c), t.dim, "(other zone)");
                    continue;
                }
                const float hy = c + t.S(t.hpY) - t.S(3);
                ember::Bar(ctx, gui, ImVec2(bx, hy), ImVec2(bw, hpH), m.hpp, m.hpp < t.lowHp ? t.emberLow : t.ember);
                gui->PushFont(nullptr, std::max(9.0f, t.S(11)));
                const float ty = hy + (hpH - gui->GetTextLineHeight()) * 0.5f;
                ember::Text(gui, ImVec2(bx + t.S(8), ty), t.ivory, std::to_string(m.hp).c_str());
                char pct[8]{};
                ::sprintf_s(pct, "%d%%", static_cast<int>(std::lround(m.hpp * 100.0f)));
                ember::TextRight(gui, bx + bw - t.S(8), ty, t.ivory, pct);
                gui->PopFont();
                const float my = c + t.S(t.mpTpY) - t.S(3);
                ember::Bar(ctx, gui, ImVec2(bx, my), ImVec2(bw * t.mpSplit, thin), m.mpp, t.mp, ember::TrackKind::Faint);
                ember::Bar(ctx, gui, ImVec2(bx + bw * t.mpSplit + t.S(6), my), ImVec2(bw * (1 - t.mpSplit) - t.S(6), thin),
                    static_cast<float>(m.tp) / 3000.0f, m.tp >= 1000 ? t.ember : t.amber, ember::TrackKind::Faint);
                gui->SetCursorScreenPos(ImVec2(o.x, y));
                gui->PushID(static_cast<int>(i));
                if (gui->InvisibleButton("##member", ImVec2(w, std::max(1.0f, rowH - 2))) && !demo)
                    ctx.core->GetChatManager()->QueueCommand(-1, ("/ta " + m.name).c_str());
                if (gui->IsItemHovered())
                {
                    char tip[64]{};
                    ::sprintf_s(tip, "%s  MP %.0f%%  TP %u", m.name.c_str(), m.mpp * 100.0f, m.tp);
                    gui->SetTooltip("%s", tip);
                }
                gui->PopID();
            }
            if (ms.empty())
                ember::Text(gui, o, t.dim, "No party data.");
            gui->SetCursorScreenPos(o);
            gui->Dummy(ImVec2(0, 0));
        }
        mod::EndPanel(p, &user_, docked_, ctx);
        if (buffs_)
            RenderFxLane(ctx, gui, demo);
    }

    // One line per member through the buff lane, anchored next to the party and flowing left:
    //   [debuffs] [+N] [lowest-priority buffs ... Haste Refresh Protect Shell] | Name
    // 18 px icons, 14 px when crowded; past that the lowest buffs fold into +N (hover: full list).
    void PartyModule::RenderFxLane(Context& ctx, IGuiManager* gui, bool demo)
    {
        const auto& t    = ember::T();
        const Rect& lane = ctx.layout.buffLane;
        if (lane.w <= t.S(t.iconSize) || rowsY_.empty())
            return;
        const double now = NowSec();
        // Your own effects carry real timers; the longest time seen is taken as the full duration.
        std::unordered_map<uint16_t, double> selfLeft;
        if (!demo)
            for (const auto& b : mod::PlayerBuffs(ctx))
                if (b.seconds > 0)
                {
                    selfLeft[b.id] = std::max(selfLeft[b.id], b.seconds);
                    double& top    = fxMax_[b.id];
                    if (b.seconds > top + 1.0 || top <= 0)
                        top = b.seconds; // new (or re-cast): this is the full length
                }
        std::vector<MemberFx> lines;
        for (size_t i = 0; i < rowsY_.size(); ++i)
        {
            const auto& r = rowsY_[i];
            if (demo)
            {
                lines.push_back(DemoFx(static_cast<int>(i), now));
                continue;
            }
            MemberFx m;
            for (const uint16_t id : Buffs(ctx, r.slot, r.serverId))
            {
                const bool deb = IsDebuff(id);
                double left = -1, total = 0;
                if (r.slot == 0)
                    if (auto it = selfLeft.find(id); it != selfLeft.end())
                        left = it->second, total = fxMax_[id];
                (deb ? m.debuffs : m.buffs).push_back({id, deb, Prio(id, deb), left, total});
            }
            SortFx(m.buffs);
            SortFx(m.debuffs);
            lines.push_back(std::move(m));
        }

        gui->SetNextWindowPos(ImVec2(lane.x, lane.y), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(lane.w + t.S(t.laneGapRight), lane.h), ImGuiCond_Always);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar;
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        const ImVec2 mouse = gui->GetIO().MousePos;
        int hoverPill = -1;
        ImVec2 pillA, pillB;
        if (gui->Begin("##fpui_fxlane", nullptr, f))
        {
            const float g = t.S(t.iconGap), th = std::max(1.0f, t.S(t.timerH)), tg = t.S(t.timerGap);
            const std::string* tip = nullptr;
            std::string tipText;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const auto& r = rowsY_[i];
                const auto& m = lines[i];
                if (i > 0 && ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, ImVec2(lane.x, r.y), ImVec2(lane.w + t.S(t.laneGapRight), 1), ImVec2(0, 0), ImVec2(1, 1), t.rowSep);
                const LineFit fit = FitLine(static_cast<int>(m.buffs.size()), static_cast<int>(m.debuffs.size()), lane.w, t);
                const float sz = fit.size;
                const float y  = r.y + (r.h - sz - tg - th) * 0.5f;
                float x        = lane.x + lane.w;
                auto icon = [&](const Fx& e) {
                    x -= sz;
                    DrawFx(ctx, gui, ImVec2(x, y), sz, e, now);
                    if (mouse.x >= x && mouse.x < x + sz && mouse.y >= y && mouse.y < y + sz + tg + th)
                    {
                        char b[96]{};
                        if (e.left >= 0)
                            ::sprintf_s(b, "%s  %d:%02d", mod::BuffName(ctx, e.id).c_str(), static_cast<int>(e.left) / 60, static_cast<int>(e.left) % 60);
                        else
                            ::sprintf_s(b, "%s", mod::BuffName(ctx, e.id).c_str());
                        tipText = b;
                        tip     = &tipText;
                    }
                    x -= g;
                };
                const int keep = static_cast<int>(m.buffs.size()) - fit.collapsed;
                for (int k = 0; k < keep; ++k)
                    icon(m.buffs[static_cast<size_t>(k)]);
                if (fit.collapsed > 0)
                {
                    const float pw = sz + t.S(t.overflowExtra);
                    x -= pw;
                    const ImVec2 a(x, y), b(x + pw, y + sz);
                    ember::RoundRect(gui, a, ImVec2(pw, sz), t.overflowFill, t.overflowEdge, sz * 0.5f, 1.0f);
                    char n[8]{};
                    ::sprintf_s(n, "+%d", fit.collapsed);
                    gui->PushFont(nullptr, std::max(9.0f, sz * 0.72f));
                    ember::TextCenter(gui, a.x + pw * 0.5f, a.y + (sz - gui->GetTextLineHeight()) * 0.5f, t.overflowText, n);
                    gui->PopFont();
                    if (mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y)
                        hoverPill = static_cast<int>(i), pillA = a, pillB = b;
                    x -= g;
                }
                for (const auto& e : m.debuffs)
                    icon(e);
            }
            if (tip != nullptr && hoverPill < 0 && popMember_ < 0)
                gui->SetTooltip("%s", tip->c_str());
        }
        gui->Dummy(ImVec2(0, 0));
        gui->End();
        gui->PopStyleVar();

        // +N popup: opens after 150 ms on the pill, stays while the mouse is on the pill or the popup,
        // closes 200 ms after it leaves.
        if (hoverPill >= 0)
        {
            if (popHoverMember_ != hoverPill)
                popHoverMember_ = hoverPill, popHoverSince_ = now;
            if (now - popHoverSince_ >= 0.15)
                popMember_ = hoverPill, popA_ = pillA, popB_ = pillB, popLast_ = now;
        }
        else
            popHoverMember_ = -1;
        if (popMember_ < 0 || popMember_ >= static_cast<int>(lines.size()))
        {
            popMember_ = -1;
            return;
        }
        const auto& m = lines[static_cast<size_t>(popMember_)];
        std::vector<const Fx*> all;
        for (const auto& e : m.buffs)
            all.push_back(&e);
        for (const auto& e : m.debuffs)
            all.push_back(&e);
        const int cols = std::max(1, t.popupMaxCols), n = static_cast<int>(all.size()), rows = (n + cols - 1) / cols;
        const float sz = t.S(t.iconSize), g = t.S(4), pad = t.S(8), head = t.S(16), rowStep = sz + t.S(6) + g;
        const float pw = cols * (sz + g) - g + 2 * pad, ph = rows * rowStep - g + 2 * pad + head;
        const ImVec2 a(std::clamp(popA_.x, 4.0f, ctx.layout.screenW - pw - 4.0f), std::max(4.0f, popA_.y - ph - t.S(6)));
        const bool over = (mouse.x >= a.x && mouse.x < a.x + pw && mouse.y >= a.y && mouse.y < a.y + ph) ||
                          (mouse.x >= popA_.x && mouse.x < popB_.x && mouse.y >= popA_.y && mouse.y < popB_.y);
        if (over)
            popLast_ = now;
        else if (now - popLast_ > 0.2)
        {
            popMember_ = -1;
            return;
        }
        // A tooltip window: ImGui always draws tooltips above every other window.
        gui->SetNextWindowPos(a, ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
        gui->PushStyleColor(ImGuiCol_PopupBg, ToVec4(Rgba{t.glass.r, t.glass.g, t.glass.b, 0.96f}));
        gui->PushStyleColor(ImGuiCol_Border, ToVec4(t.edge));
        gui->PushStyleVar(ImGuiStyleVar_PopupRounding, t.S(10));
        gui->PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (gui->BeginTooltip())
        {
            const std::string who = popMember_ < static_cast<int>(rowsY_.size()) && !demo
                                        ? std::string(ctx.core->GetMemoryManager()->GetParty()->GetMemberName(rowsY_[static_cast<size_t>(popMember_)].slot))
                                        : "Effects";
            ember::Text(gui, ImVec2(a.x + pad, a.y + pad - t.S(2)), t.amber, who.c_str());
            for (int i = 0; i < n; ++i)
                DrawFx(ctx, gui, ImVec2(a.x + pad + static_cast<float>(i % cols) * (sz + g), a.y + pad + head + static_cast<float>(i / cols) * rowStep), sz,
                    *all[static_cast<size_t>(i)], now);
            gui->SetCursorScreenPos(a);
            gui->Dummy(ImVec2(pw, ph));
            gui->EndTooltip();
        }
        gui->PopStyleVar(3);
        gui->PopStyleColor(2);
    }

    void PartyModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "party", show_, docked_, locked_, user_);
        buffs_  = cfg->GetBool(alias, "party", "buffs", buffs_);
        footer_ = cfg->GetBool(alias, "party", "footer", footer_);
    }
    void PartyModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "party", show_, docked_, locked_, user_);
        ui::SetB(cfg, alias, "party", "buffs", buffs_);
        ui::SetB(cfg, alias, "party", "footer", footer_);
    }

    auto PartyModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (!args.empty() && ui::Lower(args[0]) == "fxdemo")
        {
            testUntil_ = NowSec() + 30.0; // sample party + effect lines (4, 17, 6, 13, 35, 4 effects) for 30 s
            ctx.Print("Party: effects demo for 30 seconds.");
            return true;
        }
        if (!args.empty() && ui::Lower(args[0]) == "test")
        {
            testUntil_ = NowSec() + 20.0; // sample party for 20 seconds (Ember Dock party)
            return true;
        }
        return mod::DockCommand(ctx, "party", args, show_, docked_, locked_);
    }

    void PartyModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        bool ch = g.Toggle("Buff icons", "Each member's buffs flowing left of the frame (Ember Dock).", buffs_);
        ch |= g.Toggle("Out-of-zone lines", "Members in another zone at the bottom, one line each with their zone (Ember Dock).", footer_);
        if (ch)
            ctx.MarkDirty();
    }

    void PartyModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (party == nullptr)
            return;
        if (ember::On() && docked_)
        {
            RenderEmber(ctx, gui);
            return;
        }
        const Palette& c = *ctx.palette;
        rowsY_.clear();
        auto p = mod::BeginPanel(ctx, gui, "##fpui_party", nullptr, docked_, ctx.layout.party, user_, locked_, 0.85f, true);
        if (p.open)
        {
            const uint16_t myZone = party->GetMemberZone(0);
            int shown = 0;
            for (uint32_t i = 0; i < 6; ++i) // your party; alliance parties have their own panels
            {
                if (party->GetMemberIsActive(i) == 0)
                    continue;
                const char* name = party->GetMemberName(i);
                if (name == nullptr || name[0] == '\0')
                    continue;
                ++shown;
                gui->PushID(static_cast<int>(i));
                gui->TextColored(ToVec4(i == 0 ? art::gold_hi : art::amber), "%s", name);
                gui->SameLine(std::max(110.0f, gui->GetContentRegionAvail().x * 0.55f));
                const uint8_t mj = party->GetMemberMainJob(i), sj = party->GetMemberSubJob(i);
                if (sj != 0)
                    gui->TextColored(ToVec4(c.muted), "%s%u/%s%u", JobName(mj), party->GetMemberMainJobLevel(i), JobName(sj),
                        party->GetMemberSubJobLevel(i));
                else
                    gui->TextColored(ToVec4(c.muted), "%s%u", JobName(mj), party->GetMemberMainJobLevel(i));
                if (party->GetMemberZone(i) != myZone)
                {
                    gui->TextColored(ToVec4(c.faint), "(other zone)");
                }
                else
                {
                    const float hp = static_cast<float>(party->GetMemberHPPercent(i)) / 100.0f;
                    const float mp = static_cast<float>(party->GetMemberMPPercent(i)) / 100.0f;
                    const uint32_t tp = party->GetMemberTP(i);
                    char ov[32]{};
                    ::sprintf_s(ov, "%u", party->GetMemberHP(i));
                    ArtBar(ctx, gui, hp, hp > 0.25f ? "fill_hp_green" : "fill_hp_red", hp > 0.5f ? c.success : c.danger, 12, ov);
                    ::sprintf_s(ov, "%u", party->GetMemberMP(i));
                    ArtBar(ctx, gui, mp, "fill_mp_blue", art::orb_cyan, 9, "");
                    if (gui->IsItemHovered())
                        gui->SetTooltip("%s MP", ov);
                    ArtBar(ctx, gui, static_cast<float>(tp) / 3000.0f, "fill_tp_gold", art::gold_hi, 5, "");
                    if (gui->IsItemHovered())
                        gui->SetTooltip("%u TP", tp);
                }
                gui->PopID();
            }
            if (shown == 0)
                gui->TextDisabled("No party data.");
        }
        mod::EndPanel(p, &user_, docked_, ctx);
        RenderBuffs(ctx, gui);
    }
} // namespace farplaneui
