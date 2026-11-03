/**
 * Farplane UI - ready check, wire-compatible with XIUI's party-chat ready check.
 *
 * Everything travels as plain party chat, so members running XIUI or FarplaneUI see the same check:
 *   start      "/p Ready Check! Are you ready? Answer Yes, No, or /"  (a member's line starting "Ready Check!")
 *   ready      a member's line equal to "yes" (any case) or starting with "/" or "\"
 *   not ready  a member's line equal to "no" (any case)
 *   summary    optional "/p Players not ready: ..." from the starter when the check ends
 * Party lines arrive as "(Name) text" in chat modes 5 / 13.
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kStartText = "Ready Check! Are you ready? Answer Yes, No, or /";
        constexpr double kDuration       = 30.0;
        constexpr double kLinger         = 8.0; // results stay up after the check ends

        auto Now(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        auto Same(const std::string& a, const std::string& b) -> bool { return ui::Lower(a) == ui::Lower(b); }
        auto Trim(std::string s) -> std::string
        {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.pop_back();
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                ++i;
            return s.substr(i);
        }
        constexpr const char* kJobs[] = {"---", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM", "NIN", "DRG",
            "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN"};
        constexpr Rgba kReady{0.55f, 0.82f, 0.62f, 1};
        constexpr Rgba kNotReady{0.92f, 0.45f, 0.40f, 1};
        const Rgba kWaiting{art::amber.r, art::amber.g, art::amber.b, 0.75f};
    } // namespace

    void ReadyCheckModule::Begin(Context& ctx, const std::string& initiator)
    {
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        members_.clear();
        for (uint32_t s = 0; s < 18; ++s)
            if (party->GetMemberIsActive(s) != 0 && party->GetMemberName(s) != nullptr && party->GetMemberName(s)[0] != '\0')
                members_.push_back({party->GetMemberName(s), party->GetMemberMainJob(s), Same(party->GetMemberName(s), initiator) ? 1 : 0});
        initiator_ = initiator;
        preview_   = false;
        startedAt_ = Now();
        active_    = true;
        open_      = true;
        mine_      = party->GetMemberName(0) != nullptr && Same(party->GetMemberName(0), initiator);
        summarySent_ = false;
    }

    void ReadyCheckModule::Send(Context& ctx, const std::string& text)
    {
        if (auto* chat = ctx.core->GetChatManager(); chat != nullptr)
            chat->QueueCommand(-1, ("/p " + text).c_str());
    }

    void ReadyCheckModule::Answer(Context& ctx, bool ready)
    {
        if (preview_)
        {
            // Preview: answer locally, never to party chat.
            if (auto* party = ctx.core->GetMemoryManager()->GetParty(); party != nullptr && party->GetMemberName(0) != nullptr)
                for (auto& m : members_)
                    if (Same(m.name, party->GetMemberName(0)))
                        m.state = ready ? 1 : 2;
            return;
        }
        Send(ctx, ready ? "Yes" : "No");
    }

    auto ReadyCheckModule::IncomingText(Context& ctx, int32_t mode, const char* message, bool) -> bool
    {
        if (!enabled_ || message == nullptr || chat::Categorize(mode) != chat::Cat::Party)
            return false;
        for (const auto& line : chat::Decode(ctx.core->GetChatManager(), message))
        {
            // "(Name) text"
            const std::string t = chat::PlainText(line);
            const size_t open = t.find('('), close = t.find(") ");
            if (open != 0 || close == std::string::npos)
                continue;
            const std::string sender = t.substr(1, close - 1);
            const std::string text   = Trim(t.substr(close + 2));
            if (text.rfind("Ready Check!", 0) == 0)
            {
                Begin(ctx, sender);
                continue;
            }
            if (!active_)
                continue;
            int state = 0;
            if (Same(text, "yes") || (!text.empty() && (text[0] == '/' || text[0] == '\\')))
                state = 1;
            else if (Same(text, "no"))
                state = 2;
            if (state != 0)
                for (auto& m : members_)
                    if (Same(m.name, sender))
                        m.state = state;
        }
        return false;
    }

    void ReadyCheckModule::Frame(Context& ctx)
    {
        if (!active_)
            return;
        bool all = !members_.empty();
        for (const auto& m : members_)
            all &= m.state != 0;
        if (all || Now() - startedAt_ > kDuration)
        {
            active_   = false;
            endedAt_  = Now();
            if (mine_ && summary_ && !summarySent_)
            {
                std::string names;
                for (const auto& m : members_)
                    if (m.state != 1)
                        names += (names.empty() ? "" : ", ") + m.name;
                if (!names.empty())
                    Send(ctx, "Players not ready: " + names);
                summarySent_ = true;
            }
        }
    }

    void ReadyCheckModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!open_)
            return;
        const double now = Now();
        if (!active_ && now - endedAt_ > kLinger)
        {
            open_ = false;
            return;
        }
        if (user_.x < 0)
            user_ = Rect{ctx.layout.screenW * 0.16f, ctx.layout.screenH * 0.18f, 230.0f, 60.0f};
        auto p = mod::BeginPanel(ctx, gui, "##fpui_readycheck", nullptr, false, Rect{}, user_, false, 0.88f, false, ImGuiWindowFlags_AlwaysAutoResize);
        if (p.open)
        {
            const float w = 210.0f;
            int ready = 0, notReady = 0, waiting = 0;
            for (const auto& m : members_)
                (m.state == 1 ? ready : m.state == 2 ? notReady : waiting)++;
            gui->TextColored(ToVec4(art::gold_hi), "READY CHECK");
            char right[24]{};
            if (active_)
                ::sprintf_s(right, "%ds", static_cast<int>(std::ceil(std::max(0.0, kDuration - (now - startedAt_)))));
            else
                ::sprintf_s(right, "%s", waiting == 0 && notReady == 0 ? "All ready" : "Done");
            gui->SameLine(w - gui->CalcTextSize(right).x);
            gui->TextColored(ToVec4(art::orb_cyan), "%s", right);
            gui->TextColored(ToVec4(kWaiting), "%s asks: are you ready?", initiator_.c_str());
            gui->TextColored(ToVec4(kReady), "Ready %d/%d", ready, static_cast<int>(members_.size()));
            gui->SameLine(0, 10);
            gui->TextColored(ToVec4(kNotReady), "Not ready %d", notReady);
            gui->SameLine(0, 10);
            gui->TextColored(ToVec4(kWaiting), "Waiting %d", waiting);
            ui::AccentLine(gui, ctx, art::ember);
            for (const auto& m : members_)
            {
                const char* job = m.job < sizeof(kJobs) / sizeof(kJobs[0]) ? kJobs[m.job] : "";
                gui->TextColored(ToVec4(Rgba{0.96f, 0.95f, 0.9f, 1}), "%s", m.name.c_str());
                gui->SameLine(120);
                gui->TextColored(ToVec4(kWaiting), "%s", job);
                const char* st  = m.state == 1 ? "Ready" : m.state == 2 ? "Not ready" : "Waiting";
                gui->SameLine(w - gui->CalcTextSize(st).x);
                gui->TextColored(ToVec4(m.state == 1 ? kReady : m.state == 2 ? kNotReady : kWaiting), "%s", st);
            }
            ui::AccentLine(gui, ctx, art::ember);
            // My answer (while I'm still waiting), then Close.
            bool waitingOnMe = false;
            if (auto* party = ctx.core->GetMemoryManager()->GetParty(); party != nullptr && party->GetMemberName(0) != nullptr)
                for (const auto& m : members_)
                    if (Same(m.name, party->GetMemberName(0)) && m.state == 0)
                        waitingOnMe = true;
            if (active_ && waitingOnMe)
            {
                if (gui->Button("Yes##rc", ImVec2(w * 0.5f - 4, 0)))
                    Answer(ctx, true);
                gui->SameLine(0, 8);
                if (gui->Button("No##rc", ImVec2(w * 0.5f - 4, 0)))
                    Answer(ctx, false);
            }
            if (gui->Button("Close##rc", ImVec2(w, 0)))
                open_ = active_ = false;
        }
        mod::EndPanel(p, &user_, false, ctx);
    }

    void ReadyCheckModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "readycheck";
        enabled_ = cfg->GetBool(alias, S, "enabled", enabled_);
        summary_ = cfg->GetBool(alias, S, "summary", summary_);
        ui::LoadRect(cfg, alias, S, "win", user_);
    }
    void ReadyCheckModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "readycheck";
        ui::SetB(cfg, alias, S, "enabled", enabled_);
        ui::SetB(cfg, alias, S, "summary", summary_);
        ui::SaveRect(cfg, alias, S, "win", user_);
    }
    auto ReadyCheckModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string("start") : ui::Lower(args[0]);
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        if (sub == "start")
        {
            if (!enabled_)
                ctx.Print("Ready check is disabled. Enable it under Ready Check in /fpui.");
            else if (active_)
                ctx.Print("A ready check is already active. Use /readycheck close to dismiss it.");
            else
            {
                int members = 0;
                for (uint32_t s = 0; s < 18; ++s)
                    members += party->GetMemberIsActive(s) != 0 ? 1 : 0;
                if (members < 2)
                    ctx.Print("Join a party or alliance before starting a ready check.");
                else
                    Send(ctx, kStartText); // our own echo starts the check here, like everyone else's
            }
        }
        else if (sub == "yes" || sub == "no")
            Answer(ctx, sub == "yes");
        else if (sub == "close")
            open_ = active_ = false;
        else if (sub == "test")
        {
            // Local preview only: nothing is sent.
            const char* me = party->GetMemberName(0) != nullptr ? party->GetMemberName(0) : "You";
            members_ = {{me, 5, 0}, {"Warrior", 1, 1}, {"Whitemage", 3, 1}, {"Bard", 10, 2}, {"Samurai", 12, 0}, {"Corsair", 17, 0}};
            initiator_ = "Warrior";
            startedAt_ = Now();
            active_ = open_ = true;
            mine_    = false;
            preview_ = true;
        }
        else if (sub == "reset")
        {
            user_.x = -1;
            ctx.MarkDirty();
        }
        else
            ctx.Print("/readycheck [start|yes|no|close|test|reset]  (also /fpui readycheck ...)");
        return true;
    }
    void ReadyCheckModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Receive and start party-chat ready checks (works with XIUI users).", enabled_);
        ch |= g.Toggle("Send summary", "When your own check ends, send the players who weren't ready to party chat.", summary_);
        if (g.Button("Start", "Start a ready check for your party / alliance.", "Start"))
            Command(ctx, {"start"});
        if (g.Button("Preview", "Show a local sample (nothing is sent).", "Test"))
            Command(ctx, {"test"});
        g.Info("Answering", "Yes / No buttons, /readycheck yes|no, or type Yes, No or / in party chat.");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
