/**
 * Farplane UI - points strip.
 *
 * Port of the points addon by Shinzaku (Ashita4-Addons, BSD-style licence), itself based on the
 * Pointwatch addon by Byrthnoth. Same data and layout as the user's points bar:
 *   [Job Lv] XP cur/next [merits] (XP/hr) XP chain > n (time) | CP JP (JP/hr) CP chain (lv 99) |
 *   with the XP (or LP) progress bar underneath. (Gil lives under the equipment grid.)
 * XP, LP and merits are read from IPlayer each frame; kills and chains come from packet 0x02D
 * (offsets as in points.lua, converted to 0-based).
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kJobs[] = {"---", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM", "NIN", "DRG",
            "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN"};

        // Experience chain windows by job level (seconds for chain 0..5+), from points' globals.lua.
        struct ChainWindow
        {
            int maxLevel;
            int seconds[6];
        };
        constexpr ChainWindow kXpChain[] = {{9, {50, 40, 30, 20, 10, 10}}, {19, {100, 80, 60, 40, 20, 20}}, {29, {150, 120, 90, 60, 30, 30}},
            {39, {200, 160, 120, 80, 40, 40}}, {49, {250, 200, 150, 100, 50, 50}}, {59, {300, 240, 180, 120, 60, 60}},
            {99, {350, 280, 210, 140, 70, 70}}};

        auto Now(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        auto U16(const uint8_t* d, uint32_t size, uint32_t o) -> uint32_t { return o + 2 <= size ? static_cast<uint32_t>(d[o] | (d[o + 1] << 8)) : 0; }
        auto U32(const uint8_t* d, uint32_t size, uint32_t o) -> uint32_t
        {
            return o + 4 <= size ? static_cast<uint32_t>(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<uint32_t>(d[o + 3]) << 24)) : 0;
        }
        // 1234567 -> "1,234,567"
        auto Sep(uint64_t v) -> std::string
        {
            std::string s = std::to_string(v);
            for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
                s.insert(static_cast<size_t>(i), ",");
            return s;
        }
        // 12345 -> "12.3k"
        auto Abbrev(double v) -> std::string
        {
            char b[32]{};
            if (v >= 1000000.0)
                ::sprintf_s(b, "%.1fM", v / 1000000.0);
            else if (v >= 1000.0)
                ::sprintf_s(b, "%.1fk", v / 1000.0);
            else
                ::sprintf_s(b, "%.0f", v);
            return b;
        }
        // Average value per kill over the average gap (including the time since the last kill), per hour.
        auto PerHour(const std::vector<PointsModule::Kill>& kills, double last) -> double
        {
            if (kills.empty())
                return 0.0;
            double value = 0, gaps = 0;
            for (const auto& k : kills)
                value += k.value, gaps += k.gap;
            const double avgGap = (gaps + (Now() - last)) / static_cast<double>(kills.size() + 1);
            return avgGap > 0 ? value / static_cast<double>(kills.size()) * 3600.0 / avgGap : 0.0;
        }
        void AddKill(std::vector<PointsModule::Kill>& kills, double& last, uint32_t value)
        {
            const double now = Now();
            kills.push_back({last != 0 ? now - last : 1.0, value});
            if (kills.size() > 200)
                kills.erase(kills.begin());
            last = now;
        }

        // Lays text segments out left to right on one row.
        struct Line
        {
            IGuiManager* gui;
            void Text(const std::string& t, const Rgba& c)
            {
                gui->TextColored(ToVec4(c), "%s", t.c_str());
                gui->SameLine(0.0f, 6.0f);
            }
            void Div(void)
            {
                gui->TextColored(ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.35f}), "|");
                gui->SameLine(0.0f, 6.0f);
            }
        };
    } // namespace

    void PointsModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "points", show_, docked_, locked_, user_);
    }
    void PointsModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "points", show_, docked_, locked_, user_);
    }
    auto PointsModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "reset")
        {
            xpKills_.clear(), cpKills_.clear();
            lastXpKill_ = lastCpKill_ = 0;
            ctx.Print("Points: rates reset.");
            return true;
        }
        if (mod::DockCommand(ctx, "points", args, show_, docked_, locked_))
            return true;
        ctx.Print("/fpui points [show|hide|dock|undock|lock|unlock] | reset (clears XP/hr and CP/hr)");
        return true;
    }
    void PointsModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        if (g.Button("Reset rates", "Clear the XP/hr and CP/hr history.", "Reset"))
        {
            xpKills_.clear(), cpKills_.clear();
            lastXpKill_ = lastCpKill_ = 0;
        }
        g.Info("Credits", "Port of the points addon (Shinzaku), based on Pointwatch by Byrthnoth.");
    }

    void PointsModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        auto* mm     = ctx.core->GetMemoryManager();
        auto* party  = mm != nullptr ? mm->GetParty() : nullptr;
        auto* player = mm != nullptr ? mm->GetPlayer() : nullptr;
        if (data == nullptr || party == nullptr || player == nullptr)
            return;
        if (id == 0x02D) // action message: points gained
        {
            if (U32(data, size, 0x04) != party->GetMemberServerId(0))
                return;
            const uint32_t value = U32(data, size, 0x10), chain = U32(data, size, 0x14);
            const uint32_t msg   = U16(data, size, 0x18) % 1024;
            if (msg == 8 || msg == 105 || msg == 253 || msg == 371 || msg == 372) // experience / limit points
            {
                AddKill(xpKills_, lastXpKill_, value);
                xpChain_ = chain;
                const int level = player->GetMainJobLevel();
                int window      = 60;
                for (const auto& w : kXpChain)
                    if (level <= w.maxLevel)
                    {
                        window = w.seconds[std::min<uint32_t>(chain, 5)];
                        break;
                    }
                xpChainEnd_ = Now() + window;
            }
            else if (msg == 718 || msg == 735) // capacity points
            {
                AddKill(cpKills_, lastCpKill_, value);
                cpChain_    = chain;
                cpChainEnd_ = Now() + 30.0;
            }
        }
    }

    void PointsModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm     = ctx.core->GetMemoryManager();
        auto* player = mm != nullptr ? mm->GetPlayer() : nullptr;
        if (player == nullptr)
            return;
        const double now = Now();
        if (now > xpChainEnd_)
            xpChain_ = 0;
        if (now > cpChainEnd_)
            cpChain_ = 0;

        auto p = mod::BeginPanel(ctx, gui, "##fpui_points", nullptr, docked_, ctx.layout.points, user_, locked_, 0.82f, false);
        if (p.open)
        {
            const uint8_t job  = player->GetMainJob();
            const int level    = player->GetMainJobLevel();
            const uint32_t exp = player->GetExpCurrent(), tnl = player->GetExpNeeded();
            // Limit point mode, as points decides it.
            const bool lp = exp == 55999 || ((player->GetIsLimitModeEnabled() || player->GetIsExperiencePointsLocked()) && level >= 75);
            const uint32_t limit = player->GetLimitPoints();
            const int merits = player->GetMeritPoints(), meritsMax = player->GetMeritPointsMax();

            const ImVec2 row = gui->GetCursorScreenPos();
            Line l{gui};
            char b[96]{};
            ::sprintf_s(b, "%s %d", job < sizeof(kJobs) / sizeof(kJobs[0]) ? kJobs[job] : "???", level);
            l.Text(b, art::ember);
            l.Text(lp ? "LP " + Sep(limit) + "/10,000" : "XP " + Sep(exp) + "/" + Sep(tnl), art::gold_hi);
            l.Text("[" + std::to_string(merits) + "]", merits >= meritsMax && meritsMax > 0 ? art::ember : art::amber);
            const double xpHour = PerHour(xpKills_, lastXpKill_);
            l.Text(lp ? "(" + Abbrev(xpHour / 10000.0) + " merits/hr)" : "(" + Abbrev(xpHour) + " XP/hr)", art::amber);
            const int xpLeft = static_cast<int>(std::max(0.0, xpChainEnd_ - now));
            ::sprintf_s(b, "%s chain %u (%ds)", lp ? "LP" : "XP", xpChain_, xpLeft);
            l.Text(b, xpChain_ > 0 || xpLeft > 0 ? art::orb_cyan : Rgba{art::amber.r, art::amber.g, art::amber.b, 0.6f});
            if (level >= 99)
            {
                l.Div();
                const uint32_t cp = player->GetCapacityPoints(job), jp = player->GetJobPoints(job);
                l.Text("CP " + Sep(cp) + "/30,000", art::gold_hi);
                l.Text("JP " + std::to_string(jp), art::amber);
                l.Text("(" + Abbrev(PerHour(cpKills_, lastCpKill_) / 30000.0) + " JP/hr)", art::amber);
                ::sprintf_s(b, "CP chain %u (%ds)", cpChain_, static_cast<int>(std::max(0.0, cpChainEnd_ - now)));
                l.Text(b, cpChain_ > 0 ? art::orb_cyan : Rgba{art::amber.r, art::amber.g, art::amber.b, 0.6f});
            }

            const float frac = lp ? static_cast<float>(limit) / 10000.0f : tnl > 0 ? static_cast<float>(exp) / static_cast<float>(tnl) : 0.0f;
            gui->SetCursorScreenPos(ImVec2(row.x, row.y + gui->GetTextLineHeight() + 4.0f)); // same rows as the target bar
            mod::StripBar(ctx, gui, frac, art::ember);
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }
} // namespace farplaneui
