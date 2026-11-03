/**
 * Farplane UI - party module (anchored bottom-right in the mask; buffs will flow left of it).
 */
#include "common.hpp"

#include <cmath>
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

    // Party rows (incoming ember_dock_wiring (4) WIRING.md P1-P7): name block, then HP and MP pills with their
    // current values and a thin TP line; no MP pill for jobs without MP (a taller HP pill instead). HP colour by
    // health, a ghost chunk after damage, TP number + glow at 1000+, out-of-range / other-zone members faded,
    // the current target outlined. Bars go through the Bar Style setting when it styles meters, else the Ember pill.
    void PartyModule::RenderEmber(Context& ctx, IGuiManager* gui)
    {
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm->GetParty();
        auto* ent   = mm->GetEntity();
        const auto& t = ember::T();
        struct M
        {
            uint32_t slot, serverId;
            std::string name, job;
            uint32_t hp, mp, tp;
            float hpp, mpp;
            bool hasMp, otherZone, faded, target;
            bool leader{false}, sync{false}; // party leader (flag 0x04 / leader id), level sync target (flag 0x100)
            bool pet{false};                 // your pet's row (HP / MP are percents: the game sends no values for pets)
        };
        // Jobs with MP (main or sub): WHM BLM RDM PLD DRK SMN BLU SCH GEO RUN.
        auto mpJob = [](uint8_t j) { return j == 3 || j == 4 || j == 5 || j == 7 || j == 8 || j == 15 || j == 16 || j == 20 || j == 21 || j == 22; };
        std::vector<M> ms;
        const bool demo = NowSec() < testUntil_;
        if (demo)
        {
            ms = {{0, 1, "Retainer", "NIN8/BRD4", 1310, 0, 1200, 0.82f, 0.0f, false, false, false, false, true, false},
                {9, 2, "Kelz", "WHM7/BLM3", 682, 812, 600, 0.66f, 0.80f, true, false, false, true, false, true},
                {9, 3, "Eddythegreat", "WAR7/WHM3", 1328, 96, 300, 0.75f, 0.55f, true, false, false, false},
                {9, 4, "Cypher", "PLD7/WAR3", 354, 220, 2100, 0.24f, 0.95f, true, false, false, false},
                {9, 5, "Ayame", "SAM7/WAR3", 1402, 0, 2900, 0.91f, 0.0f, false, false, true, false},
                {9, 6, "Lyra", "RDM7/BLM3", 988, 512, 100, 0.55f, 0.60f, true, true, true, false}};
            ms.insert(ms.begin() + 1, M{99, 7, "Carbuncle", "pet", 76, 40, 450, 0.76f, 0.40f, true, false, false, false, false, false, true});
        }
        else
        {
            const uint16_t myZone = party->GetMemberZone(0);
            auto* tgt = mm->GetTarget();
            const uint32_t tIdx = tgt != nullptr ? tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u) : 0;
            if (myZone != ghostZone_)
                ghost_.clear(), mpSeen_.clear(), ghostZone_ = myZone; // reset on zone
            for (uint32_t i = 0; i < 6; ++i)
            {
                const char* name = party->GetMemberName(i);
                if (party->GetMemberIsActive(i) == 0 || name == nullptr || name[0] == '\0')
                    continue;
                char job[32]{};
                const uint8_t mj = party->GetMemberMainJob(i), sj = party->GetMemberSubJob(i), sjl = party->GetMemberSubJobLevel(i);
                if (sj != 0 && sjl != 0)
                    ::sprintf_s(job, "%s%u/%s%u", JobName(mj), party->GetMemberMainJobLevel(i), JobName(sj), sjl);
                else
                    ::sprintf_s(job, "%s%u", JobName(mj), party->GetMemberMainJobLevel(i));
                const uint32_t sid = party->GetMemberServerId(i), mp = party->GetMemberMP(i);
                const bool other = party->GetMemberZone(i) != myZone;
                // Once a member shows MP it keeps showing for the zone (no flicker when drained to 0).
                const bool hasMp = mpJob(mj) || (sj != 0 && sjl != 0 && mpJob(sj)) || mp > 0 || mpSeen_.count(sid) != 0;
                if (hasMp)
                    mpSeen_.insert(sid);
                // Out of range: not loaded / not drawn, or past 20.8 yalms (you never are).
                const uint32_t ti = party->GetMemberTargetIndex(i);
                bool faded = other;
                if (i > 0 && !other)
                    faded = ti == 0 || ti >= 2304 || ent->GetRawEntity(ti) == nullptr || (ent->GetRenderFlags0(ti) & 0x4200u) != 0x200u ||
                            std::sqrt(std::max(0.0f, ent->GetDistance(ti))) > 20.8f;
                // Leader / sync: the server's party member flags (0x04 party leader, 0x100 level sync target; as XivParty
                // reads them), the leader also by this party's leader server id.
                const uint32_t flags = party->GetMemberFlagMask(i);
                const bool leader    = (flags & 0x04u) != 0 || (sid != 0 && sid == party->GetAlliancePartyLeaderServerId1());
                ms.push_back({i, sid, name, job, party->GetMemberHP(i), mp, party->GetMemberTP(i), static_cast<float>(party->GetMemberHPPercent(i)) / 100.0f,
                    static_cast<float>(party->GetMemberMPPercent(i)) / 100.0f, hasMp, other, faded, ti != 0 && ti == tIdx, leader, (flags & 0x100u) != 0});
            }
        }
        // Your pet: a row right under yours (name, HP %, MP %, TP; IEntity pet index, IPlayer pet MP / TP).
        if (!demo)
        {
            const uint32_t me  = party->GetMemberTargetIndex(0);
            const uint32_t pet = me != 0 && me < 2304 && ent->GetRawEntity(me) != nullptr ? ent->GetPetTargetIndex(me) : 0;
            auto* player       = mm->GetPlayer();
            if (!petLog_.empty()) // /fpui party petlog: one line per change of what decides the pet row
            {
                const bool raw = pet != 0 && pet < 2304 && ent->GetRawEntity(pet) != nullptr;
                char b[360]{};
                ::sprintf_s(b, "me %u -> pet index %u%s | name \"%s\" hp %u%% type %u spawn 0x%X render 0x%X claim 0x%X | player pet mp %u%% tp %u", me, pet,
                    raw ? "" : " (no entity)", raw && ent->GetName(pet) != nullptr ? ent->GetName(pet) : "", raw ? ent->GetHPPercent(pet) : 0u,
                    raw ? static_cast<unsigned>(ent->GetType(pet)) : 0u, raw ? static_cast<unsigned>(ent->GetSpawnFlags(pet)) : 0u,
                    raw ? static_cast<unsigned>(ent->GetRenderFlags0(pet)) : 0u, raw ? static_cast<unsigned>(ent->GetClaimStatus(pet)) : 0u,
                    player != nullptr ? player->GetPetMPPercent() : 0u, player != nullptr ? player->GetPetTP() : 0u);
                std::string line = b;
                // Monsters claimed by you (a charmed pet may show up here if the pet index stays 0).
                const uint32_t myId = party->GetMemberServerId(0);
                for (uint32_t e = 0; e < 0x400; ++e)
                    if (e != pet && ent->GetRawEntity(e) != nullptr && (ent->GetSpawnFlags(e) & 0x10u) != 0 && ent->GetHPPercent(e) > 0 &&
                        (ent->GetClaimStatus(e) & 0x00FFFFFFu) == (myId & 0x00FFFFFFu) && myId != 0)
                    {
                        char c[96]{};
                        ::sprintf_s(c, " | claimed by you: %u \"%s\" claim 0x%X", e, ent->GetName(e) != nullptr ? ent->GetName(e) : "", static_cast<unsigned>(ent->GetClaimStatus(e)));
                        line += c;
                    }
                if (line != lastPetLine_)
                {
                    lastPetLine_ = line;
                    FILE* f = nullptr;
                    if (::fopen_s(&f, petLog_.c_str(), "ab") == 0 && f != nullptr)
                    {
                        ::fprintf(f, "%llu  %s\r\n", static_cast<unsigned long long>(::GetTickCount64()), line.c_str());
                        ::fclose(f);
                    }
                }
            }
            if (pet != 0 && pet < 2304 && ent->GetRawEntity(pet) != nullptr && ent->GetHPPercent(pet) > 0 && ent->GetName(pet) != nullptr && player != nullptr)
            {
                const uint32_t sid = ent->GetServerId(pet);
                const uint8_t hpp = ent->GetHPPercent(pet), mpp = player->GetPetMPPercent();
                if (mpp > 0)
                    mpSeen_.insert(sid);
                auto* tgt = mm->GetTarget();
                const uint32_t tIdx = tgt != nullptr ? tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u) : 0;
                M row{99, sid, ent->GetName(pet), "pet", hpp, mpp, player->GetPetTP(), hpp / 100.0f, mpp / 100.0f, mpSeen_.count(sid) != 0, false, false,
                    tIdx == pet, false, false, true};
                ms.insert(ms.empty() ? ms.end() : ms.begin() + 1, row);
            }
        }
        rowsY_.clear();
        const size_t rowsN = std::max<size_t>(6, ms.size()); // six members plus a pet squeeze to seven rows
        const float rowH   = ctx.layout.party.h / static_cast<float>(rowsN);
        // Vertical layout in 1080p px for a 44 px row (40 + 4 gap), squeezed when the dock row is shorter.
        const float k = std::min(1.0f, rowH / t.S(44.0f));
        auto V = [&](float v) { return t.S(v) * k; };
        // HP / MP / TP must keep their own colours: only a style that colours by meaning (Framed with "colour by
        // meaning") is used here. Pill / Chamfer are ready-made coloured strips (every bar would look the same), so
        // the party draws its own pill in the meaning colours instead.
        const bool styled = mod::Bars().meters && mod::Bars().shape == 1 && mod::Bars().tint;
        const double now  = NowSec();
        const Rgba ivory = t.ivory, gold = ember::Hex(0xf2c45aff), track = ember::Hex(0x0a0c18c8);
        // HP colour by health: >= 75% ember, 50-75% amber -> ember, 25-50% bad -> amber, < 25% bad.
        auto hpColor = [&](float h) -> Rgba {
            auto lerp = [](const Rgba& a, const Rgba& b, float f) { return Rgba{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1.0f}; };
            if (h >= 0.75f)
                return t.ember;
            if (h >= 0.50f)
                return lerp(t.amber, t.ember, (h - 0.50f) / 0.25f);
            if (h >= 0.25f)
                return lerp(t.bad, t.amber, (h - 0.25f) / 0.25f);
            return t.bad;
        };
        // One bar: track, ghost (0 .. ghost), fill; through the bar style if it styles meters.
        auto bar = [&](const ImVec2& pos, const ImVec2& size, float frac, float ghost, const Rgba& col, float a) {
            frac = std::clamp(frac, 0.0f, 1.0f);
            if (styled)
            {
                mod::DrawBar(ctx, gui, pos, size, frac, col, a, true);
                if (ghost > frac) // the part just lost, over the track
                    ember::RoundRect(gui, ImVec2(pos.x + size.x * frac, pos.y), ImVec2(size.x * (ghost - frac), size.y), Rgba{ivory.r, ivory.g, ivory.b, 0.35f * a},
                        Rgba{0, 0, 0, 0}, 0.0f, 0.0f);
                return;
            }
            ember::RoundRect(gui, pos, size, Rgba{track.r, track.g, track.b, track.a * a}, Rgba{t.edgeDim.r, t.edgeDim.g, t.edgeDim.b, t.edgeDim.a * a}, size.y * 0.5f, 1.0f);
            if (ghost > frac)
                ember::Bar(ctx, gui, pos, size, ghost, Rgba{ivory.r, ivory.g, ivory.b, 0.35f}, ember::TrackKind::None, a);
            if (frac > 0.0f)
                ember::Bar(ctx, gui, pos, size, frac, col, ember::TrackKind::None, a);
        };

        auto p = mod::BeginPanel(ctx, gui, "##fpui_party", nullptr, docked_, ctx.layout.party, user_, locked_, 0.0f, false, ImGuiWindowFlags_NoScrollWithMouse);
        if (p.open)
        {
            const ImVec2 o = gui->GetWindowPos();
            const float w = ctx.layout.party.w;
            panelX_ = o.x;
            const float nameW = t.S(130.0f);
            const float bx = o.x + nameW;
            gui->PushFont(nullptr, std::max(9.0f, t.S(11)));
            const float numW = gui->CalcTextSize("99999").x + t.S(6);
            gui->PopFont();
            const float bw = std::max(t.S(60.0f), std::min(t.S(230.0f), w - nameW - numW));
            for (size_t i = 0; i < ms.size() && i < rowsN; ++i)
            {
                const M& m    = ms[i];
                const float y = o.y + rowH * static_cast<float>(i);
                const float c = y + std::max(0.0f, (rowH - V(40.0f)) * 0.5f); // row content top
                const float a = m.faded ? 0.40f : 1.0f;
                if (!m.pet) // the buff lane lines up with party members only
                    rowsY_.push_back({m.slot, m.serverId, y, rowH});
                if (i > 0 && ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, ImVec2(o.x, y), ImVec2(w, 1), ImVec2(0, 0), ImVec2(1, 1), t.rowSep);
                // Current target: soft outline behind name + bars, and a chevron left of the name.
                if (m.target)
                {
                    ember::RoundRect(gui, ImVec2(o.x - t.S(4), c - V(2)), ImVec2(nameW + bw + numW + t.S(6), V(40)), ember::Hex(0xe6b47912), ember::Hex(0xe6b47978),
                        t.S(8), 1.0f);
                    const float cy = c + V(10), cx = o.x - t.S(12);
                    for (int s = 0; s < 6; ++s) // 7 x 12 chevron from stacked strips
                    {
                        const float f = static_cast<float>(s) / 5.0f;
                        ui::ImageAt(gui, ctx.white, ImVec2(cx + t.S(7) * f, cy - t.S(6) * (1 - f)), ImVec2(t.S(1.5f), t.S(12) * (1 - f) + t.S(1)), ImVec2(0, 0), ImVec2(1, 1),
                            t.amber, a);
                    }
                }
                // Name block: name (13 bold), job line (10 muted); "out of range" / zone tag after the name.
                gui->PushFont(nullptr, std::max(11.0f, t.S(13)));
                // Leader star and level sync arrows after the name (16 px, 5 px gap; leader first).
                const float isz  = t.S(16.0f) * std::max(0.75f, k);
                const int icons  = (m.leader ? 1 : 0) + (m.sync ? 1 : 0);
                const std::string nm = Fit(gui, m.name, nameW - t.S(8) - static_cast<float>(icons) * (isz + t.S(5)));
                const float nameY = c + V(4) - t.S(2);
                const float nx    = o.x + (m.pet ? t.S(12) : 0.0f); // the pet's name sits indented under yours
                ember::Text(gui, ImVec2(nx, nameY), Rgba{ivory.r, ivory.g, ivory.b, a}, nm.c_str());
                float ix = nx + gui->CalcTextSize(nm.c_str()).x + t.S(5);
                const float iy = nameY + (gui->GetTextLineHeight() - isz) * 0.5f;
                gui->PopFont();
                for (const char* file : {m.leader ? "leader" : nullptr, m.sync ? "sync" : nullptr})
                {
                    if (file == nullptr)
                        continue;
                    if (IDirect3DTexture8* tex = ctx.textures->File(ctx.resourceDir + "ember_dock\\icons\\" + file + ".png"); tex != nullptr)
                        ui::ImageAt(gui, tex, ImVec2(ix, iy), ImVec2(isz, isz), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1}, a);
                    ix += isz + t.S(5);
                }
                gui->PushFont(nullptr, std::max(9.0f, t.S(10)));
                ember::Text(gui, ImVec2(nx, c + V(22)), Rgba{t.muted.r, t.muted.g, t.muted.b, a}, m.job.c_str());
                gui->PopFont();
                if (m.faded)
                {
                    gui->PushFont(nullptr, std::max(8.0f, t.S(9)));
                    ember::Text(gui, ImVec2(bx, c + V(31) + t.S(1)), Rgba{t.dim.r, t.dim.g, t.dim.b, 0.8f}, m.otherZone ? "other zone" : "out of range");
                    gui->PopFont();
                }
                // Ghost chunk: on damage keep the old HP, hold 0.4 s, drain to the current HP over 0.6 s.
                float ghost = m.hpp;
                if (!m.otherZone)
                {
                    Ghost& g = ghost_[m.serverId];
                    if (m.hpp < g.last - 0.001f)
                        g.ghost = std::max(g.ghost, g.last), g.at = now; // new hit: keep the higher ghost, restart the hold
                    else if (m.hpp > g.last)
                        g.ghost = m.hpp; // heals snap
                    g.last = m.hpp;
                    const double age = now - g.at;
                    if (age > 1.0 || g.ghost <= m.hpp)
                        g.ghost = m.hpp;
                    else if (age > 0.4)
                        ghost = m.hpp + (g.ghost - m.hpp) * static_cast<float>(1.0 - (age - 0.4) / 0.6);
                    else
                        ghost = g.ghost;
                }
                const float hp = m.otherZone ? 0.0f : m.hpp;
                const Rgba hc  = hpColor(m.hpp);
                // Low HP pulse (0.8 - 1.0 at 1.5 Hz).
                const float pulse = (!m.otherZone && m.hpp < 0.25f && m.hpp > 0.0f) ? 0.9f + 0.1f * static_cast<float>(std::sin(now * 6.2831853 * 1.5)) : 1.0f;
                char num[16]{};
                if (m.hasMp)
                {
                    bar(ImVec2(bx, c + V(4)), ImVec2(bw, V(9)), hp, ghost, hc, a * pulse);
                    bar(ImVec2(bx, c + V(16)), ImVec2(bw, V(9)), m.otherZone ? 0.0f : m.mpp, 0.0f, t.mp, a);
                    gui->PushFont(nullptr, std::max(9.0f, t.S(11)));
                    ::sprintf_s(num, m.pet ? "%u%%" : "%u", m.hp);
                    ember::Text(gui, ImVec2(bx + bw + t.S(6), c + V(4) + (V(9) - gui->GetTextLineHeight()) * 0.5f), Rgba{ivory.r, ivory.g, ivory.b, a}, num);
                    ::sprintf_s(num, m.pet ? "%u%%" : "%u", m.mp);
                    ember::Text(gui, ImVec2(bx + bw + t.S(6), c + V(16) + (V(9) - gui->GetTextLineHeight()) * 0.5f), Rgba{t.mp.r, t.mp.g, t.mp.b, a}, num);
                    gui->PopFont();
                }
                else
                {
                    bar(ImVec2(bx, c + V(8)), ImVec2(bw, V(21)), hp, ghost, hc, a * pulse);
                    gui->PushFont(nullptr, std::max(10.0f, t.S(12)));
                    ::sprintf_s(num, m.pet ? "%u%%" : "%u", m.hp);
                    ember::Text(gui, ImVec2(bx + bw + t.S(6), c + V(8) + (V(21) - gui->GetTextLineHeight()) * 0.5f), Rgba{ivory.r, ivory.g, ivory.b, a}, num);
                    gui->PopFont();
                }
                // TP: thin line, min(tp, 1000) / 1000; at 1000+ gold with a glow and the number.
                if (!m.otherZone)
                {
                    const bool full = m.tp >= 1000;
                    const float tf  = std::min(m.tp, 1000u) / 1000.0f;
                    if (full)
                        ember::Bar(ctx, gui, ImVec2(bx - t.S(1.5f), c + V(31) - t.S(1.5f)), ImVec2(bw + t.S(3), V(3) + t.S(3)), 1.0f, Rgba{gold.r, gold.g, gold.b, 0.30f},
                            ember::TrackKind::None, a);
                    bar(ImVec2(bx, c + V(31)), ImVec2(bw, std::max(2.0f, V(3))), tf, 0.0f, full ? gold : Rgba{t.amber.r, t.amber.g, t.amber.b, 0.67f}, a);
                    if (full)
                    {
                        gui->PushFont(nullptr, std::max(8.0f, t.S(10)));
                        ::sprintf_s(num, "%u", m.tp);
                        ember::Text(gui, ImVec2(bx + bw + t.S(6), c + V(31) + V(1.5f) - gui->GetTextLineHeight() * 0.5f), Rgba{gold.r, gold.g, gold.b, a}, num);
                        gui->PopFont();
                    }
                }
                gui->SetCursorScreenPos(ImVec2(o.x, y));
                gui->PushID(static_cast<int>(i));
                if (gui->InvisibleButton("##member", ImVec2(w, std::max(1.0f, rowH - 2))) && !demo)
                    ctx.core->GetChatManager()->QueueCommand(-1, m.pet ? "/ta <pet>" : ("/ta " + m.name).c_str());
                if (gui->IsItemHovered())
                {
                    char tip[160]{};
                    ::sprintf_s(tip, "%s%s%s  HP %u (%.0f%%)  MP %u (%.0f%%)  TP %u", m.name.c_str(), m.leader ? "  (leader)" : "", m.sync ? "  (level sync)" : "",
                        m.hp, m.hpp * 100.0f, m.mp, m.mpp * 100.0f, m.tp);
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
        if (!args.empty() && ui::Lower(args[0]) == "petlog")
        {
            if (petLog_.empty())
            {
                const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
                ::CreateDirectoryA(dir.c_str(), nullptr);
                petLog_ = dir + "\\farplaneui_pet.log";
                ::DeleteFileA(petLog_.c_str());
                lastPetLine_.clear();
                ctx.Print("Party: logging your pet to " + petLog_ + ". /fpui party petlog again to stop.");
            }
            else
            {
                ctx.Print("Party: pet log off. Log: " + petLog_);
                petLog_.clear();
            }
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
