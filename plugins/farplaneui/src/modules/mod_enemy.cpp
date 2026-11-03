/**
 * Farplane UI - enemy list (left side).
 *
 * Live monsters engaged with the party: claimed by a party member, targeting one, or seen acting on one
 * in the last 15 seconds (action packets; catches aggro before the claim lands). Each row: name in its
 * claim colour, -> who it is targeting, distance, HP bar and percentage. Your current target is
 * outlined; clicking a row targets that monster.
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr double kAggroMemory = 15.0;
        constexpr Rgba kClaimedParty{0.95f, 0.42f, 0.36f, 1.0f};
        constexpr Rgba kClaimedOther{0.78f, 0.55f, 0.95f, 1.0f};

        auto Now(void) -> double
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
    } // namespace

    // A monster acting on a party member (action packet: actor @40, first target @150).
    void EnemyModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr)
            return;
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        const uint32_t actor = Bits(data, size, 40, 32), target = Bits(data, size, 150, 32);
        for (uint32_t s = 0; s < 18; ++s)
            if (party->GetMemberIsActive(s) != 0 && party->GetMemberServerId(s) == target)
            {
                aggro_[actor] = Now();
                return;
            }
    }

    void EnemyModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm    = ctx.core->GetMemoryManager();
        auto* ent   = mm != nullptr ? mm->GetEntity() : nullptr;
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        auto* tgt   = mm != nullptr ? mm->GetTarget() : nullptr;
        if (ent == nullptr || party == nullptr || tgt == nullptr)
            return;
        const double now = Now();
        for (auto it = aggro_.begin(); it != aggro_.end();)
            it = now - it->second > kAggroMemory ? aggro_.erase(it) : std::next(it);

        // Party members by server id and target index.
        uint32_t ids[18]{}, idx[18]{};
        uint32_t members = 0;
        for (uint32_t s = 0; s < 18; ++s)
            if (party->GetMemberIsActive(s) != 0)
                ids[members] = party->GetMemberServerId(s), idx[members] = party->GetMemberTargetIndex(s), ++members;
        auto inParty = [&](uint32_t serverId, uint32_t index) {
            for (uint32_t m = 0; m < members; ++m)
                if ((serverId != 0 && ids[m] == serverId) || (index != 0 && idx[m] == index))
                    return true;
            return false;
        };

        // Display rows: live monsters, or samples while /fpui enemies test runs.
        struct Show
        {
            uint32_t index; // 0 = sample (not clickable)
            std::string name, target;
            Rgba color;
            float distance;
            int hp;
            bool current;
        };
        std::vector<Show> rows;
        const uint32_t current = tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u);
        if (now < testUntil_)
        {
            const char* me = party->GetMemberName(0) != nullptr ? party->GetMemberName(0) : "You";
            rows.push_back({0, "Orcish Grunt", me, kClaimedParty, 3.2f, 82, true});
            rows.push_back({0, "Wild Rabbit", me, kClaimedParty, 5.8f, 55, false});
            rows.push_back({0, "Goblin Ambusher", me, kClaimedOther, 9.4f, 28, false});
            rows.push_back({0, "Stone Eater", "", art::gold_hi, 14.0f, 100, false});
        }
        else
        {
            for (const uint32_t i : mod::NearbyMobs(ctx))
            {
                if (ent->GetRawEntity(i) == nullptr || (ent->GetSpawnFlags(i) & 0x10u) == 0 || (ent->GetRenderFlags0(i) & 0x4200u) != 0x200u ||
                    ent->GetHPPercent(i) == 0)
                    continue;
                const uint32_t claim = ent->GetClaimStatus(i);
                const bool claimed   = inParty(claim, 0);
                const bool fighting  = inParty(0, ent->GetTargetIndex(i)) && ent->GetStatus(i) == 1; // engaged on a party member
                const bool attacked  = aggro_.count(ent->GetServerId(i)) != 0;
                if (!claimed && !fighting && !attacked)
                    continue;
                const uint32_t ti = ent->GetTargetIndex(i);
                const char* tn    = ti != 0 && ti < 2304 && ent->GetRawEntity(ti) != nullptr ? ent->GetName(ti) : nullptr;
                rows.push_back({i, ent->GetName(i) != nullptr ? ent->GetName(i) : "?", tn != nullptr ? tn : "",
                    claim == 0 ? art::gold_hi : claimed ? kClaimedParty : kClaimedOther, std::sqrt(std::max(0.0f, ent->GetDistance(i))),
                    static_cast<int>(ent->GetHPPercent(i)), i == current});
            }
        }
        if (rows.empty())
            return;
        std::sort(rows.begin(), rows.end(), [](const Show& a, const Show& b) { return a.distance < b.distance; });
        if (static_cast<int>(rows.size()) > maxRows_)
            rows.resize(static_cast<size_t>(maxRows_));

        if (user_.x < 0)
            user_ = Rect{18.0f, ctx.layout.screenH * 0.18f, 250.0f, 60.0f};
        auto p = mod::BeginPanel(ctx, gui, "##fpui_enemies", nullptr, false, Rect{}, user_, locked_, 0.78f, false,
            ImGuiWindowFlags_AlwaysAutoResize);
        if (p.open)
        {
            // Width follows the widest row: name, a gap, then "-> target  distance".
            auto rightText = [](const Show& r) {
                char b[64]{};
                if (!r.target.empty())
                    ::sprintf_s(b, "-> %s  %.1f'", r.target.c_str(), static_cast<double>(r.distance));
                else
                    ::sprintf_s(b, "%.1f'", static_cast<double>(r.distance));
                return std::string(b);
            };
            float w = 240.0f;
            for (const auto& r : rows)
                w = std::max(w, gui->CalcTextSize(r.name.c_str()).x + 16.0f + gui->CalcTextSize(rightText(r).c_str()).x);
            const float lineH = gui->GetTextLineHeight(), barW = w - 34.0f;
            for (const auto& r : rows)
            {
                const ImVec2 top = gui->GetCursorScreenPos();
                const float rowH = lineH + 4.0f + mod::kStripBarH + 6.0f;
                if (r.current)
                    ui::ImageAt(gui, ctx.white, ImVec2(top.x - 4, top.y - 2), ImVec2(w + 8, rowH), ImVec2(0, 0), ImVec2(1, 1), art::gold_hi, 0.12f);
                // Row 1: name ............ -> target  distance
                gui->TextColored(ToVec4(r.color), "%s", r.name.c_str());
                const std::string right = rightText(r);
                gui->SameLine(w - gui->CalcTextSize(right.c_str()).x);
                gui->TextColored(ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.85f}), "%s", right.c_str());
                // Row 2: HP bar with percentage.
                const ImVec2 bp = gui->GetCursorScreenPos();
                const float hp  = r.hp / 100.0f;
                const Rgba fill = hp > 0.5f ? Rgba{0.55f, 0.82f, 0.62f, 1.0f} : hp > 0.25f ? art::amber : ctx.palette->danger;
                mod::DrawBar(ctx, gui, bp, ImVec2(barW, mod::kStripBarH), hp, fill, 1.0f, true);
                char hpText[8]{};
                ::sprintf_s(hpText, "%d%%", r.hp);
                gui->SetCursorScreenPos(ImVec2(bp.x + w - gui->CalcTextSize(hpText).x, bp.y + (mod::kStripBarH - lineH) * 0.5f));
                gui->TextColored(ToVec4(art::gold_hi), "%s", hpText);
                // Click anywhere on a live row to target it.
                gui->SetCursorScreenPos(top);
                gui->PushID(static_cast<int>(&r - rows.data()));
                if (gui->InvisibleButton("##enemy", ImVec2(w, rowH - 2)) && clickTarget_ && r.index != 0)
                    tgt->SetTarget(r.index, false);
                gui->PopID();
                gui->SetCursorScreenPos(ImVec2(top.x, top.y + rowH));
            }
            gui->Dummy(ImVec2(w, 0));
        }
        mod::EndPanel(p, &user_, false, ctx);
    }

    void EnemyModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "enemies";
        show_        = cfg->GetBool(alias, S, "show", show_);
        locked_      = cfg->GetBool(alias, S, "locked", locked_);
        clickTarget_ = cfg->GetBool(alias, S, "click", clickTarget_);
        maxRows_     = std::clamp(cfg->GetInt32(alias, S, "rows", maxRows_), 1, 16);
        ui::LoadRect(cfg, alias, S, "win", user_);
    }
    void EnemyModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "enemies";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "locked", locked_);
        ui::SetB(cfg, alias, S, "click", clickTarget_);
        ui::SetI(cfg, alias, S, "rows", maxRows_);
        ui::SaveRect(cfg, alias, S, "win", user_);
    }
    auto EnemyModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
            show_ = sub == "toggle" ? !show_ : sub == "show";
        else if (sub == "lock" || sub == "unlock")
            locked_ = sub == "lock";
        else if (sub == "reset")
            user_.x = -1;
        else if (sub == "test")
        {
            testUntil_ = Now() + 15.0; // sample rows for 15 seconds
            return true;
        }
        else
        {
            ctx.Print("/fpui enemies [show|hide|lock|unlock|reset] | test (sample rows for 15s)");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }
    void EnemyModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Monsters engaged with your party, on the left of the screen.", show_);
        ch |= g.Toggle("Locked", "Stop the list being dragged.", locked_);
        ch |= g.Toggle("Click to target", "Click a row to target that monster.", clickTarget_);
        ch |= g.Slider("Maximum rows", "Most monsters listed (closest first).", maxRows_, 1, 16);
        if (g.Button("Preview", "Show sample rows for 15 seconds.", "Test"))
            testUntil_ = Now() + 15.0;
        if (g.Button("Position", "Put the list back at its default spot.", "Reset"))
        {
            user_.x = -1;
            ch      = true;
        }
        g.Info("Listed when", "Claimed by your party, targeting a party member, or attacked one in the last 15 seconds.");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
