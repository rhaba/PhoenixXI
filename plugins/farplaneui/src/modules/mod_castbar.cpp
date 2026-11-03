/**
 * Farplane UI - cast bar (centred below the character).
 *
 * Progress is the game's own cast bar (ICastBar percent). The name comes from our own action packets:
 * category 8 (spell) / 9 (item) carry the spell or item id in the first action's param; for spells the
 * packet id says start (24931) or interrupted (28787). Bit layout: actor @40, category @82, id @86
 * (17 bits), first target @150, its first action's param @213 (17 bits).
 */
#include "common.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr uint32_t kCastStart       = 24931;
        constexpr uint32_t kCastInterrupted = 28787;
        constexpr double kHold              = 1.2; // seconds a finished / interrupted bar lingers

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

    void CastBarModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr)
            return;
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        if (Bits(data, size, 40, 32) != party->GetMemberServerId(0))
            return;
        const uint32_t category = Bits(data, size, 82, 4), action = Bits(data, size, 86, 17), param = Bits(data, size, 213, 17);
        auto* res = ctx.core->GetResourceManager();
        if (category == 8)
        {
            if (action == kCastInterrupted)
            {
                interruptedAt_ = Now();
                return;
            }
            if (action != kCastStart)
                return;
            ISpell* s = res->GetSpellById(param);
            name_ = s != nullptr && s->Name[0] != nullptr && s->Name[0][0] != '\0' ? s->Name[0] : "Casting";
            item_ = false;
            interruptedAt_ = 0;
        }
        else if (category == 9 && items_)
        {
            IItem* it = res->GetItemById(param);
            name_ = it != nullptr && it->Name[0] != nullptr ? it->Name[0] : "Item";
            item_ = true;
            interruptedAt_ = 0;
        }
    }

    void CastBarModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm  = ctx.core->GetMemoryManager();
        auto* bar = mm != nullptr ? mm->GetCastBar() : nullptr;
        if (bar == nullptr)
            return;
        const double now = Now();
        float pct = bar->GetPercent();
        if (pct > 1.0f) // some clients report 0..100
            pct /= 100.0f;
        const bool casting = pct > 0.0f && pct < 1.0f;
        if (casting)
        {
            lastPct_  = pct;
            lastSeen_ = now;
        }
        const bool interrupted = interruptedAt_ > 0 && now - interruptedAt_ < kHold;
        const bool lingering   = !casting && now - lastSeen_ < kHold && lastPct_ > 0;
        if (!casting && !interrupted && !lingering)
            return;
        const float shown = casting ? pct : interrupted ? lastPct_ : 1.0f;
        const float alpha = casting ? 1.0f : static_cast<float>(std::clamp(kHold - (now - (interrupted ? interruptedAt_ : lastSeen_)), 0.0, 1.0));

        const float w = width_, h = 10.0f;
        const float x = ctx.layout.screenW * 0.5f - w * 0.5f + offsetX_;
        const float y = ctx.layout.screenH * 0.60f + offsetY_;
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_castbar", nullptr, f))
        {
            const Rgba fill = interrupted ? ctx.palette->danger : item_ ? art::amber : Rgba{0.45f, 0.62f, 0.95f, 1};
            const float lineH = gui->GetTextLineHeight();
            // Name above the bar on the left, percentage on the right.
            std::string label = interrupted ? "Interrupted" : name_.empty() ? std::string("Casting") : name_;
            char pctText[16]{};
            ::sprintf_s(pctText, "%d%%", static_cast<int>(shown * 100.0f + 0.5f));
            for (int pass = 0; pass < 2; ++pass)
            {
                const float o = pass == 0 ? 1.0f : 0.0f;
                const ImVec4 col  = pass == 0 ? ImVec4(0, 0, 0, 0.8f * alpha) : ImVec4(0.96f, 0.95f, 0.9f, alpha);
                const ImVec4 pcol = pass == 0 ? ImVec4(0, 0, 0, 0.8f * alpha) : ToVec4(Rgba{art::gold_hi.r, art::gold_hi.g, art::gold_hi.b, alpha});
                gui->SetCursorScreenPos(ImVec2(x + o, y - lineH - 2 + o));
                gui->TextColored(interrupted && pass == 1 ? ToVec4(Rgba{fill.r, fill.g, fill.b, alpha}) : col, "%s", label.c_str());
                if (showPercent_)
                {
                    gui->SetCursorScreenPos(ImVec2(x + w - gui->CalcTextSize(pctText).x + o, y - lineH - 2 + o));
                    gui->TextColored(pcol, "%s", pctText);
                }
            }
            mod::DrawBar(ctx, gui, ImVec2(x, y), ImVec2(w, h), shown, fill, alpha, false);
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void CastBarModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "castbar";
        show_        = cfg->GetBool(alias, S, "show", show_);
        items_       = cfg->GetBool(alias, S, "items", items_);
        showPercent_ = cfg->GetBool(alias, S, "percent", showPercent_);
        width_       = std::clamp(cfg->GetFloat(alias, S, "width", width_), 120.0f, 600.0f);
        offsetX_     = cfg->GetFloat(alias, S, "offsetX", offsetX_);
        offsetY_     = cfg->GetFloat(alias, S, "offsetY", offsetY_);
    }
    void CastBarModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "castbar";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "items", items_);
        ui::SetB(cfg, alias, S, "percent", showPercent_);
        ui::SetF(cfg, alias, S, "width", width_);
        ui::SetF(cfg, alias, S, "offsetX", offsetX_);
        ui::SetF(cfg, alias, S, "offsetY", offsetY_);
    }
    auto CastBarModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
        {
            show_ = sub == "toggle" ? !show_ : sub == "show";
            ctx.MarkDirty();
            return true;
        }
        ctx.Print("/fpui castbar [show|hide]");
        return true;
    }
    void CastBarModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Your cast progress, centred below the character.", show_);
        ch |= g.Toggle("Item use", "Also show item-use progress (amber).", items_);
        ch |= g.Toggle("Percentage", "Show the cast percentage.", showPercent_);
        ch |= g.Slider("Width", "Bar width in pixels.", width_, 120.0f, 600.0f, "%.0f");
        ch |= g.Slider("Horizontal offset", "Move the bar left (-) or right (+).", offsetX_, -800.0f, 800.0f, "%.0f");
        ch |= g.Slider("Vertical offset", "Move the bar up (-) or down (+).", offsetY_, -600.0f, 600.0f, "%.0f");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
