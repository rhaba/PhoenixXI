/**
 * Farplane UI - cast bar (centred below the character) or cast ring (on the ground around your feet,
 * drawn in the 3D scene like the target ring, filling clockwise as the spell casts).
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
        constexpr int kColors = 8;
        constexpr const char* kColorNames[kColors] = {"Sky blue", "Amber", "Ember", "Violet", "Frost", "Jade", "Gold", "Ivory"};
        constexpr Rgba kColorValues[kColors] = {Rgba{0.45f, 0.62f, 0.95f, 1}, hex(0xe6b479), hex(0xcf733e), hex(0xa77dff), hex(0x8ce6ff),
            hex(0x6fd3a0), hex(0xf6da93), hex(0xf3e8d8)};

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
        const Rgba fill   = interrupted ? ctx.palette->danger : kColorValues[item_ ? itemColor_ : spellColor_];

        // Ring: around your feet in the 3D scene (host draws ctx.cast).
        if (style_ != 1 && ctx.camera.Fresh())
        {
            auto* party = mm->GetParty();
            auto* ent   = mm->GetEntity();
            const uint32_t me = party != nullptr ? party->GetMemberTargetIndex(0) : 0;
            if (ent != nullptr && me != 0 && me < 2304 && ent->GetRawEntity(me) != nullptr && (ent->GetRenderFlags0(me) & 0x4200u) == 0x200u)
            {
                auto& c    = ctx.cast;
                c.want     = true;
                c.x        = ent->GetLocalPositionX(me), c.y = ent->GetLocalPositionZ(me), c.z = ent->GetLocalPositionY(me); // game world (X, height, Y)
                c.radius   = 0.9f * ringSize_;
                c.yaw      = ent->GetLocalPositionYaw(me);
                c.progress = shown;
                c.color    = fill;
                c.flash    = lingering && !interrupted;
                c.alpha    = alpha;
                c.grow     = c.flash ? static_cast<float>(std::clamp((now - lastSeen_) / kHold, 0.0, 1.0)) : 0.0f;
            }
        }
        if (style_ == 0 && !ringLabel_)
            return;

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
            if (style_ != 0)
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
        const std::string st = ui::Lower(ui::GetS(cfg, alias, S, "style", style_ == 0 ? "ring" : style_ == 1 ? "bar" : "both"));
        style_       = st == "bar" ? 1 : st == "both" ? 2 : 0;
        ringSize_    = std::clamp(cfg->GetFloat(alias, S, "ringSize", ringSize_), 0.4f, 3.0f);
        ringLabel_   = cfg->GetBool(alias, S, "ringLabel", ringLabel_);
        spellColor_  = std::clamp(cfg->GetInt32(alias, S, "spellColor", spellColor_), 0, kColors - 1);
        itemColor_   = std::clamp(cfg->GetInt32(alias, S, "itemColor", itemColor_), 0, kColors - 1);
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
        cfg->SetValue(alias, S, "style", style_ == 0 ? "ring" : style_ == 1 ? "bar" : "both");
        ui::SetF(cfg, alias, S, "ringSize", ringSize_);
        ui::SetB(cfg, alias, S, "ringLabel", ringLabel_);
        ui::SetI(cfg, alias, S, "spellColor", spellColor_);
        ui::SetI(cfg, alias, S, "itemColor", itemColor_);
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
        if (sub == "style" && args.size() > 1)
        {
            const std::string v = ui::Lower(args[1]);
            style_ = v == "bar" ? 1 : v == "both" ? 2 : 0;
            ctx.MarkDirty();
            return true;
        }
        ctx.Print("/fpui castbar [show|hide] | style ring|bar|both");
        return true;
    }
    void CastBarModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Your cast progress.", show_);
        static const char* const kStyles[] = {"Ring", "Bar", "Both"};
        ch |= g.Choice("Style", "Ring: a ring on the ground around your feet that fills clockwise as you cast. Bar: the bar below your character. Both: ring and bar.", style_, kStyles, 3);
        if (style_ != 1)
        {
            ch |= g.Slider("Ring size", "Ring radius around your character.", ringSize_, 0.4f, 3.0f, "%.2f");
            if (style_ == 0)
                ch |= g.Toggle("Ring label", "Show the spell name and percentage where the bar would be.", ringLabel_);
        }
        ch |= g.Choice("Spell colour", "Ring and bar colour while casting a spell (interrupted is always red).", spellColor_, kColorNames, kColors);
        ch |= g.Toggle("Item use", "Also show item-use progress.", items_);
        if (items_)
            ch |= g.Choice("Item colour", "Ring and bar colour while using an item.", itemColor_, kColorNames, kColors);
        ch |= g.Toggle("Percentage", "Show the cast percentage.", showPercent_);
        ch |= g.Slider("Width", "Bar width in pixels.", width_, 120.0f, 600.0f, "%.0f");
        ch |= g.Slider("Horizontal offset", "Move the bar left (-) or right (+).", offsetX_, -800.0f, 800.0f, "%.0f");
        ch |= g.Slider("Vertical offset", "Move the bar up (-) or down (+).", offsetY_, -600.0f, 600.0f, "%.0f");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
