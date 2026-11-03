/**
 * Farplane UI - tick counter (centre of the strip, between the target bar and points).
 *
 * Resting: our own implementation of the game's healing mechanics. While resting (entity status 33)
 * the first HP/MP tick lands 20 seconds after you sit down, then every 10 seconds. The timer re-syncs
 * to every tick it actually sees (HP or MP rising while resting), and the gains shown are the ones
 * measured on the last tick - gear, food and traits are all included without having to model them.
 *
 * Engaged: auto-attack rounds are our own melee action packets (0x028, category 1). Swing time and
 * TP per round are rolling averages of the last rounds, so haste and gear changes are picked up.
 */
#include "common.hpp"

#include <chrono>
#include <numeric>

namespace farplaneui
{
    namespace
    {
        constexpr uint32_t kStatusEngaged = 1;
        constexpr uint32_t kStatusResting = 33;
        constexpr double kFirstTick       = 20.0;
        constexpr double kTickInterval    = 10.0;
        constexpr size_t kAverageOver     = 8;

        auto Now(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        // Seconds -> "45s" / "2m 05s"
        auto Clock(double s) -> std::string
        {
            const int t = static_cast<int>(std::ceil(std::max(0.0, s)));
            char b[24]{};
            if (t >= 60)
                ::sprintf_s(b, "%dm %02ds", t / 60, t % 60);
            else
                ::sprintf_s(b, "%ds", t);
            return b;
        }
        template <typename T>
        auto Average(const std::deque<T>& v) -> double
        {
            return v.empty() ? 0.0 : static_cast<double>(std::accumulate(v.begin(), v.end(), T{})) / static_cast<double>(v.size());
        }
        template <typename T>
        void PushCapped(std::deque<T>& v, T x)
        {
            v.push_back(x);
            if (v.size() > kAverageOver)
                v.pop_front();
        }
        const Rgba kDim{art::amber.r, art::amber.g, art::amber.b, 0.75f};
    } // namespace

    void TicksModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "ticks", show_, docked_, locked_, user_);
    }
    void TicksModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "ticks", show_, docked_, locked_, user_);
    }
    auto TicksModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (mod::DockCommand(ctx, "ticks", args, show_, docked_, locked_))
            return true;
        ctx.Print("/fpui ticks [show|hide|dock|undock|lock|unlock]");
        return true;
    }
    void TicksModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        g.Info("Resting", "First tick at 20s, then every 10s; re-synced to each tick seen. Gains are measured.");
        char b[96]{};
        ::sprintf_s(b, "Swing time and TP per round averaged over your last %u rounds.", static_cast<unsigned>(kAverageOver));
        g.Info("Auto-attack", b);
    }

    // Our melee rounds. Action packet: actor id at bit 40 (32 bits), category at bit 82 (4 bits).
    void TicksModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr || size < 0x0B)
            return;
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (party == nullptr)
            return;
        const uint32_t actor    = static_cast<uint32_t>(data[5] | (data[6] << 8) | (data[7] << 16)) | (static_cast<uint32_t>(data[8]) << 24);
        const uint32_t category = (data[0x0A] >> 2) & 0x0Fu;
        if (category != 1 || actor != party->GetMemberServerId(0))
            return;
        const double now  = Now();
        const uint32_t tp = party->GetMemberTP(0);
        if (lastRound_ != 0)
        {
            const double gap = now - lastRound_;
            if (gap > 0.5 && gap < 12.0) // ignore breaks (disengage, stuns, out of range)
                PushCapped(swingGaps_, gap);
            if (tp > roundTp_ && tp - roundTp_ < 400) // a weapon skill spends TP; skip those rounds
                PushCapped(tpGains_, tp - roundTp_);
        }
        lastRound_ = now;
        roundTp_   = tp;
        ++rounds_;
    }

    void TicksModule::Frame(Context& ctx)
    {
        auto* mm     = ctx.core->GetMemoryManager();
        auto* party  = mm != nullptr ? mm->GetParty() : nullptr;
        auto* ent    = mm != nullptr ? mm->GetEntity() : nullptr;
        if (party == nullptr || ent == nullptr)
            return;
        const uint32_t idx    = party->GetMemberTargetIndex(0);
        const uint32_t status = idx != 0 ? ent->GetStatus(idx) : 0;
        const uint32_t hp = party->GetMemberHP(0), mp = party->GetMemberMP(0);
        const bool resting = status == kStatusResting;
        const bool engaged = status == kStatusEngaged;
        const double now   = Now();
        if (engaged && !engaged_)
            rounds_ = 0; // count rounds per engagement; swing/TP averages carry over
        engaged_ = engaged;
        if (resting && !resting_)
        {
            // Sat down: first tick after 20s. Gains from a previous rest are kept until the next tick.
            startedAt_ = now;
            nextTick_  = now + kFirstTick;
            ticks_     = 0;
        }
        else if (resting)
        {
            // A tick we can see: HP or MP rose. Re-sync the schedule to it.
            if (hp > lastHp_ || mp > lastMp_)
            {
                lastHpGain_ = hp > lastHp_ ? hp - lastHp_ : 0;
                lastMpGain_ = mp > lastMp_ ? mp - lastMp_ : 0;
                ++ticks_;
                nextTick_ = now + kTickInterval;
            }
            // At full HP and MP ticks still happen but change nothing: keep the 10s rhythm.
            while (now >= nextTick_)
                nextTick_ += kTickInterval;
        }
        resting_ = resting;
        lastHp_  = hp;
        lastMp_  = mp;
    }

    void TicksModule::Render(Context& ctx, IGuiManager* gui)
    {
        ctx.ticksPanel = show_;
        if (!show_)
            return;
        // Resting: a second line under the bar ("MP full in 42s"); the panel grows upward by it so its
        // bottom stays level with the other strip panels.
        Rect dock = ctx.layout.ticks;
        const bool casting = ctx.castStrip.want; // the cast bar takes the panel while you cast
        if (resting_ && !casting)
        {
            const float extra = gui->GetTextLineHeight() + 2.0f;
            dock.y -= extra, dock.h += extra;
        }
        auto p = mod::BeginPanel(ctx, gui, "##fpui_ticks", nullptr, docked_, dock, user_, locked_, 0.82f, false);
        if (p.open)
        {
            std::string below;
            const ImVec2 row  = gui->GetCursorScreenPos();
            const float width = gui->GetContentRegionAvail().x;
            float frac        = 0.0f;
            Rgba barColor     = art::orb_cyan;
            if (casting)
            {
                const auto& c = ctx.castStrip;
                gui->TextColored(ToVec4(c.color), "%s", c.label.c_str());
                if (!c.right.empty())
                {
                    gui->SameLine(std::max(0.0f, width - gui->CalcTextSize(c.right.c_str()).x));
                    gui->TextColored(ToVec4(art::gold_hi), "%s", c.right.c_str());
                }
                frac     = c.frac;
                barColor = c.color;
            }
            else if (engaged_)
            {
                RenderAttack(ctx, gui, width, frac);
                barColor = art::ember;
            }
            else if (resting_)
                RenderResting(ctx, gui, width, frac, below);
            else
                gui->TextDisabled("Resting  -");
            gui->SetCursorScreenPos(ImVec2(row.x, row.y + gui->GetTextLineHeight() + 4.0f)); // same rows as the strip
            mod::StripBar(ctx, gui, frac, barColor);
            if (!below.empty())
            {
                gui->SetCursorScreenPos(ImVec2(row.x, gui->GetCursorScreenPos().y + 2.0f));
                gui->TextColored(ToVec4(kDim), "%s", below.c_str());
            }
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }

    // [next tick]  MP full in Ns            +HP +MP
    void TicksModule::RenderResting(Context& ctx, IGuiManager* gui, float width, float& frac, std::string& below)
    {
        auto* player = ctx.core->GetMemoryManager()->GetPlayer();
        const double left  = nextTick_ - Now();
        const double cycle = ticks_ == 0 ? kFirstTick : kTickInterval;
        frac               = static_cast<float>(1.0 - std::clamp(left / cycle, 0.0, 1.0));
        const uint32_t hpMax = player != nullptr ? player->GetHPMax() : 0, mpMax = player != nullptr ? player->GetMPMax() : 0;
        if (lastHp_ >= hpMax && (mpMax == 0 || lastMp_ >= mpMax))
            gui->TextColored(ToVec4(Rgba{0.55f, 0.82f, 0.62f, 1.0f}), "Full");
        else
        {
            gui->TextColored(ToVec4(art::orb_cyan), "%s", Clock(left).c_str());
            // MP to full (HP for jobs without MP), from the last measured gain. Slightly long: ticks grow.
            const bool useMp     = mpMax > 0;
            const uint32_t cur   = useMp ? lastMp_ : lastHp_, max = useMp ? mpMax : hpMax;
            const uint32_t gain  = useMp ? lastMpGain_ : lastHpGain_;
            char b[48]{};
            if (cur >= max)
                ::sprintf_s(b, "%s full", useMp ? "MP" : "HP");
            else if (gain == 0)
                ::sprintf_s(b, "%s full in ...", useMp ? "MP" : "HP");
            else
            {
                const double ticksLeft = std::ceil(static_cast<double>(max - cur) / gain);
                const int secs         = static_cast<int>(std::ceil((ticksLeft - 1) * kTickInterval + std::max(0.0, left)));
                ::sprintf_s(b, "%s full in %ds", useMp ? "MP" : "HP", secs);
            }
            below = b; // drawn under the bar
        }
        char gains[48]{};
        if (lastHpGain_ > 0 || lastMpGain_ > 0)
            ::sprintf_s(gains, "+%u HP  +%u MP", lastHpGain_, lastMpGain_);
        gui->SameLine(width - gui->CalcTextSize(gains).x);
        gui->TextColored(ToVec4(art::gold_hi), "%s", gains);
    }

    // [next swing]  Round N                 1k in Ns
    void TicksModule::RenderAttack(Context& ctx, IGuiManager* gui, float width, float& frac)
    {
        auto* party       = ctx.core->GetMemoryManager()->GetParty();
        const uint32_t tp = party != nullptr ? party->GetMemberTP(0) : 0;
        const double swing = Average(swingGaps_);
        const double now   = Now();
        double left        = 0.0;
        if (swing > 0 && lastRound_ != 0 && now - lastRound_ < 12.0)
        {
            left = std::max(0.0, lastRound_ + swing - now);
            frac = static_cast<float>(1.0 - std::clamp(left / swing, 0.0, 1.0));
            gui->TextColored(ToVec4(art::orb_cyan), "%.1fs", left);
        }
        else
            gui->TextColored(ToVec4(kDim), "Swing ...");
        gui->SameLine(0.0f, 8.0f);
        gui->TextColored(ToVec4(kDim), "Round %u", rounds_);

        char right[48]{};
        const double perRound = Average(tpGains_);
        if (tp >= 1000)
            ::sprintf_s(right, "WS ready");
        else if (perRound > 0 && swing > 0)
        {
            const double roundsLeft = std::ceil((1000.0 - tp) / perRound);
            ::sprintf_s(right, "1k in %ds", static_cast<int>(std::ceil((roundsLeft - 1) * swing + left)));
        }
        if (right[0] != '\0') // no TP number: just the time to 1000 TP (or WS ready)
        {
            gui->SameLine(width - gui->CalcTextSize(right).x);
            gui->TextColored(ToVec4(tp >= 1000 ? art::ember : art::gold_hi), "%s", right);
        }
    }
} // namespace farplaneui
