/**
 * Farplane UI - alliance panels (right side), one per alliance party (slots 6-11 and 12-17).
 *
 * Each panel only appears while its party has members. Compact rows: name, job / levels, TP; a thin HP
 * bar with the HP value and an MP bar. Clicking a member targets them.
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kJobs[] = {"---", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM", "NIN", "DRG",
            "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN"};
        auto Job(uint8_t id) -> const char* { return id < sizeof(kJobs) / sizeof(kJobs[0]) ? kJobs[id] : "???"; }
        auto Now(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        struct Member
        {
            uint32_t targetIndex; // 0 = sample
            std::string name, job;
            int hpPct, mpPct;
            uint32_t hp, tp;
            bool away;
        };
        void Bar(Context& ctx, IGuiManager* gui, const ImVec2& pos, float w, float h, float frac, const Rgba& fill)
        {
            mod::DrawBar(ctx, gui, pos, ImVec2(w, h), frac, fill, 1.0f, true);
        }
    } // namespace

    void AllianceModule::RenderParty(Context& ctx, IGuiManager* gui, int which)
    {
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm->GetParty();
        auto* tgt   = mm->GetTarget();
        std::vector<Member> rows;
        if (Now() < testUntil_)
        {
            static const char* names[2][6] = {{"Whitemage", "Paladin", "Bard", "Corsair", "Samurai", "Ninja"},
                {"Monk", "Dragoon", "Thief", "Scholar", "Ranger", "Geomancer"}};
            static const char* jobs[2][6] = {{"WHM75/BLM37", "PLD75/WAR37", "BRD75/WHM37", "COR75/RNG37", "SAM75/WAR37", "NIN75/WAR37"},
                {"MNK75/WAR37", "DRG75/WAR37", "THF75/NIN37", "SCH75/RDM37", "RNG75/NIN37", "GEO75/RDM37"}};
            for (int i = 0; i < 6; ++i)
                rows.push_back({0, names[which][i], jobs[which][i], 100 - i * 13, 90 - i * 15, static_cast<uint32_t>(1500 - i * 120), static_cast<uint32_t>(i * 400), i == 5});
        }
        else
        {
            const uint16_t myZone = party->GetMemberZone(0);
            for (uint32_t s = 6u + static_cast<uint32_t>(which) * 6u; s < 12u + static_cast<uint32_t>(which) * 6u; ++s)
            {
                if (party->GetMemberIsActive(s) == 0 || party->GetMemberName(s) == nullptr || party->GetMemberName(s)[0] == '\0')
                    continue;
                char job[32]{};
                const uint8_t mj = party->GetMemberMainJob(s), sj = party->GetMemberSubJob(s);
                if (sj != 0)
                    ::sprintf_s(job, "%s%u/%s%u", Job(mj), party->GetMemberMainJobLevel(s), Job(sj), party->GetMemberSubJobLevel(s));
                else
                    ::sprintf_s(job, "%s%u", Job(mj), party->GetMemberMainJobLevel(s));
                rows.push_back({party->GetMemberTargetIndex(s), party->GetMemberName(s), job, party->GetMemberHPPercent(s), party->GetMemberMPPercent(s),
                    party->GetMemberHP(s), party->GetMemberTP(s), party->GetMemberZone(s) != myZone});
            }
        }
        if (rows.empty())
            return; // this alliance party doesn't exist

        Rect& user = user_[which];
        if (user.x < 0)
            user = Rect{ctx.layout.screenW - 250.0f - 18.0f, ctx.layout.screenH * (which == 0 ? 0.30f : 0.52f), 250.0f, 60.0f};
        char id[32]{};
        ::sprintf_s(id, "##fpui_alliance%d", which);
        auto p = mod::BeginPanel(ctx, gui, id, nullptr, false, Rect{}, user, locked_, 0.78f, false, ImGuiWindowFlags_AlwaysAutoResize);
        if (p.open)
        {
            const float w = 230.0f, lineH = gui->GetTextLineHeight();
            const uint32_t current = tgt != nullptr ? tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u) : 0;
            gui->TextColored(ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.6f}), "Alliance %d", which + 2);
            for (const auto& m : rows)
            {
                const ImVec2 top = gui->GetCursorScreenPos();
                const float rowH = lineH + 2.0f + 6.0f + 2.0f + 4.0f + 6.0f;
                if (m.targetIndex != 0 && m.targetIndex == current)
                    ui::ImageAt(gui, ctx.white, ImVec2(top.x - 4, top.y - 2), ImVec2(w + 8, rowH), ImVec2(0, 0), ImVec2(1, 1), art::gold_hi, 0.12f);
                // name .... job   TP
                gui->TextColored(ToVec4(m.away ? Rgba{0.6f, 0.6f, 0.65f, 1} : Rgba{0.96f, 0.95f, 0.9f, 1}), "%s", m.name.c_str());
                char right[48]{};
                ::sprintf_s(right, "%s  %u", m.job.c_str(), m.tp);
                gui->SameLine(w - gui->CalcTextSize(right).x);
                gui->TextColored(ToVec4(m.tp >= 1000 ? art::ember : Rgba{art::amber.r, art::amber.g, art::amber.b, 0.8f}), "%s", right);
                const ImVec2 bp(top.x, top.y + lineH + 2.0f);
                if (m.away)
                {
                    gui->SetCursorScreenPos(bp);
                    gui->TextDisabled("(other zone)");
                }
                else
                {
                    const float hp = m.hpPct / 100.0f;
                    Bar(ctx, gui, bp, w - 44.0f, 6.0f, hp, hp > 0.5f ? Rgba{0.55f, 0.82f, 0.62f, 1} : hp > 0.25f ? art::amber : ctx.palette->danger);
                    Bar(ctx, gui, ImVec2(bp.x, bp.y + 8.0f), w - 44.0f, 4.0f, m.mpPct / 100.0f, Rgba{0.45f, 0.62f, 0.95f, 1});
                    char hpText[16]{};
                    ::sprintf_s(hpText, "%u", m.hp);
                    gui->SetCursorScreenPos(ImVec2(bp.x + w - gui->CalcTextSize(hpText).x, bp.y + 5.0f - lineH * 0.5f));
                    gui->TextColored(ToVec4(art::gold_hi), "%s", hpText);
                }
                gui->SetCursorScreenPos(top);
                gui->PushID(static_cast<int>(&m - rows.data()));
                if (gui->InvisibleButton("##member", ImVec2(w, rowH - 2)) && m.targetIndex != 0 && tgt != nullptr)
                    tgt->SetTarget(m.targetIndex, false);
                gui->PopID();
                gui->SetCursorScreenPos(ImVec2(top.x, top.y + rowH));
            }
            gui->Dummy(ImVec2(w, 0));
        }
        mod::EndPanel(p, &user, false, ctx);
    }

    void AllianceModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_ || ctx.core->GetMemoryManager() == nullptr || ctx.core->GetMemoryManager()->GetParty() == nullptr)
            return;
        RenderParty(ctx, gui, 0);
        RenderParty(ctx, gui, 1);
    }

    void AllianceModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "alliance";
        show_   = cfg->GetBool(alias, S, "show", show_);
        locked_ = cfg->GetBool(alias, S, "locked", locked_);
        ui::LoadRect(cfg, alias, S, "win1", user_[0]);
        ui::LoadRect(cfg, alias, S, "win2", user_[1]);
    }
    void AllianceModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "alliance";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "locked", locked_);
        ui::SaveRect(cfg, alias, S, "win1", user_[0]);
        ui::SaveRect(cfg, alias, S, "win2", user_[1]);
    }
    auto AllianceModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
            show_ = sub == "toggle" ? !show_ : sub == "show";
        else if (sub == "lock" || sub == "unlock")
            locked_ = sub == "lock";
        else if (sub == "reset")
            user_[0].x = user_[1].x = -1;
        else if (sub == "test")
        {
            testUntil_ = Now() + 15.0;
            return true;
        }
        else
        {
            ctx.Print("/fpui alliance [show|hide|lock|unlock|reset] | test (sample panels for 15s)");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }
    void AllianceModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Panels for alliance parties 2 and 3 (only while an alliance is formed).", show_);
        ch |= g.Toggle("Locked", "Stop the panels being dragged.", locked_);
        if (g.Button("Preview", "Show both panels with sample members for 15 seconds.", "Test"))
            testUntil_ = Now() + 15.0;
        if (g.Button("Position", "Put both panels back at their default spots.", "Reset"))
        {
            user_[0].x = user_[1].x = -1;
            ch         = true;
        }
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
