/**
 * Farplane UI - target bar (top centre).
 *
 * Row 1: name (claim colour), level range, distance.   Row 2: HP bar.
 * Row 3: mobdb data - aggro/link and detection icons, then damage-taken modifiers (weaknesses in
 * green, resistances in red) and status immunities. Data and icons are built in (resources\farplaneui\mobdb,
 * see mobdb.hpp). An "i" button opens the mob info window (drops with rates, steal, respawn).
 */
#include "common.hpp"
#include "../ember.hpp"

#include <cmath>

namespace farplaneui
{
    namespace
    {
        constexpr float kIcon = 16.0f;

        struct Immunity
        {
            uint32_t flag;
            const char* icon;
            const char* label;
        };
        // mobdb details.lua bit order.
        constexpr Immunity kImmunities[] = {{0x01, "ImmuneSleep", "Sleep"}, {0x02, "ImmuneGravity", "Gravity"}, {0x04, "ImmuneBind", "Bind"},
            {0x08, "ImmuneStun", "Stun"}, {0x10, "ImmuneSilence", "Silence"}, {0x20, "ImmuneParalyze", "Paralyze"}, {0x40, "ImmuneBlind", "Blind"},
            {0x80, "ImmuneSlow", "Slow"}, {0x100, "ImmunePoison", "Poison"}, {0x200, "ImmuneElegy", "Elegy"}, {0x400, "ImmuneRequiem", "Requiem"},
            {0x800, "ImmuneLightSleep", "Light sleep"}, {0x1000, "ImmuneDarkSleep", "Dark sleep"}, {0x2000, "ImmunePetrify", "Petrify"}};

        constexpr Rgba kWeak{0.52f, 0.86f, 0.56f, 1.0f};
        constexpr Rgba kResist{0.92f, 0.45f, 0.40f, 1.0f};
        constexpr Rgba kClaimedParty{0.95f, 0.42f, 0.36f, 1.0f};
        constexpr Rgba kClaimedOther{0.78f, 0.55f, 0.95f, 1.0f};

        auto InstallDir(IAshitaCore* core) -> std::string
        {
            std::string dir = core->GetInstallPath();
            while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/'))
                dir.pop_back();
            return dir + "\\";
        }

        constexpr float kLineGap = 2.0f; // between wrapped lines

        // Icons and labels collected first, then flowed left to right, wrapping onto new lines.
        // A unit (an icon with its label) never splits across lines.
        struct Flow
        {
            struct Item
            {
                std::string file, text; // icon file, or text
                std::string tip;
                Rgba color{};
                float gap{0};      // space before
                bool join{false};  // stays on the same line as the previous item
            };
            IGuiManager* gui;
            Context& ctx;
            std::vector<Item> items;
            float pendingGap{0};

            void Icon(const std::string& file, const char* tip) { items.push_back({file, {}, tip != nullptr ? tip : "", {}, Take(), false}); }
            void Text(const char* text, const Rgba& color, bool join = false) { items.push_back({{}, text, {}, color, Take(), join}); }
            void Gap(float w) { pendingGap += w; }
            auto Take(void) -> float
            {
                const float g = pendingGap;
                pendingGap    = 0;
                return g;
            }
            auto Width(const Item& it) const -> float { return it.text.empty() ? kIcon + 2.0f : gui->CalcTextSize(it.text.c_str()).x + 6.0f; }
            auto Exists(const Item& it) const -> bool { return !it.text.empty() || ctx.textures->File(it.file) != nullptr; }

            // Lays out from `origin`; line 1 ends at `firstLimit`, later lines at `limit`. Draws when `draw`.
            // Returns the number of lines used.
            auto Layout(const ImVec2& origin, float firstLimit, float limit, float lineH, bool draw) -> int
            {
                const float rowH = std::max(lineH, kIcon) + kLineGap;
                float x = origin.x, y = origin.y;
                int lines = 1;
                for (size_t i = 0; i < items.size();)
                {
                    // Unit = this item plus any joined followers.
                    size_t end = i + 1;
                    while (end < items.size() && items[end].join)
                        ++end;
                    float w = 0;
                    bool any = false;
                    for (size_t k = i; k < end; ++k)
                        if (Exists(items[k]))
                            w += Width(items[k]), any = true;
                    if (!any)
                    {
                        i = end;
                        continue;
                    }
                    float gap = x > origin.x ? items[i].gap : 0.0f;
                    if (x > origin.x && x + gap + w > (lines == 1 ? firstLimit : limit))
                    {
                        ++lines;
                        x = origin.x, y += rowH, gap = 0;
                    }
                    x += gap;
                    for (size_t k = i; k < end; ++k)
                    {
                        const Item& it = items[k];
                        if (!Exists(it))
                            continue;
                        if (draw)
                            Draw(it, ImVec2(x, y), lineH);
                        x += Width(it);
                    }
                    i = end;
                }
                return lines;
            }
            void Draw(const Item& it, const ImVec2& p, float lineH)
            {
                if (it.text.empty())
                {
                    const ImVec2 at(p.x, p.y + (lineH - kIcon) * 0.5f);
                    ui::ImageAt(gui, ctx.textures->File(it.file), at, ImVec2(kIcon, kIcon), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                    const ImVec2 m = gui->GetIO().MousePos;
                    if (!it.tip.empty() && m.x >= at.x && m.y >= at.y && m.x < at.x + kIcon && m.y < at.y + kIcon)
                        gui->SetTooltip("%s", it.tip.c_str());
                    return;
                }
                gui->SetCursorScreenPos(p);
                gui->TextColored(ToVec4(it.color), "%s", it.text.c_str());
            }
        };
    } // namespace

    void TargetBarModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "targetbar", show_, docked_, locked_, user_);
        docked_         = cfg->GetBool(alias, "targetbar", "strip", true); // baked into the strip above the band
        showResists_    = cfg->GetBool(alias, "targetbar", "resists", showResists_);
        showImmunities_ = cfg->GetBool(alias, "targetbar", "immunities", showImmunities_);
        th_             = std::clamp(cfg->GetInt32(alias, "targetbar", "th", th_), 0, 14);
        slim_           = cfg->GetBool(alias, "targetbar", "slim", slim_);
    }
    void TargetBarModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "targetbar", show_, docked_, locked_, user_);
        ui::SetB(cfg, alias, "targetbar", "strip", docked_);
        ui::SetB(cfg, alias, "targetbar", "resists", showResists_);
        ui::SetB(cfg, alias, "targetbar", "immunities", showImmunities_);
        ui::SetI(cfg, alias, "targetbar", "th", th_);
        ui::SetB(cfg, alias, "targetbar", "slim", slim_);
    }
    auto TargetBarModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "reset")
        {
            user_.x = -1;
            ctx.MarkDirty();
            ctx.Print("Target bar: position reset to top centre.");
            return true;
        }
        if (sub == "info")
        {
            infoOpen_ = !infoOpen_;
            return true;
        }
        if (sub == "th" && args.size() > 1)
        {
            th_ = std::clamp(std::atoi(args[1].c_str()), 0, 14);
            ctx.MarkDirty();
            return true;
        }
        if (sub == "slim")
        {
            const std::string v = args.size() > 1 ? ui::Lower(args[1]) : std::string();
            slim_ = v == "on" ? true : v == "off" ? false : !slim_;
            ctx.MarkDirty();
            return true;
        }
        if (sub == "debug")
        {
            ctx.Print("Target bar mobdb: zone " + std::to_string(db_.Zone()) + ", " + (db_.Status().empty() ? std::string("not loaded") : db_.Status()));
            return true;
        }
        if (mod::DockCommand(ctx, "targetbar", args, show_, docked_, locked_))
            return true;
        ctx.Print("/fpui targetbar [show|hide|dock|undock|lock|unlock] | reset | debug");
        return true;
    }
    void TargetBarModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        bool ch = g.Toggle("Show resistances", "Damage types the target resists (weaknesses always show).", showResists_);
        ch |= g.Toggle("Show immunities", "Status effects the target is immune to.", showImmunities_);
        ch |= g.Toggle("Slim bar", "Only name, HP and distance here; level, detection and resists show on the target ring instead.", slim_);
        if (ch)
            ctx.MarkDirty();
        g.Info("Monster data", db_.Status().empty() ? "mobdb not loaded yet" : db_.Status().c_str());
        ch = g.Slider("Treasure Hunter", "Your TH level, for the drop rates in the mob info window.", th_, 0, 14);
        if (ch)
            ctx.MarkDirty();
        g.Info("Source", "Built in: resources\\farplaneui\\mobdb (from mobdb 2.x and PhoenixXI's server data; no addon needed)");
    }

    void TargetBarModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm    = ctx.core->GetMemoryManager();
        auto* ent   = mm != nullptr ? mm->GetEntity() : nullptr;
        auto* tgt   = mm != nullptr ? mm->GetTarget() : nullptr;
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (ent == nullptr || tgt == nullptr || party == nullptr)
            return;
        uint32_t idx = tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u);
        if (idx >= 2304 || (idx != 0 && ent->GetRawEntity(idx) == nullptr))
            idx = 0;
        // Doors, counters, home points, guide stones...: just the name here (Target > Show on objects shows the full
        // bar); the game's own target cursor marks them and there is no ring.
        const bool object = idx != 0 && !mod::ShowObjects() && mod::IsObject(ent, idx);
        // Baked into the strip, the panel stays up with no target so the strip doesn't jump.
        if (idx == 0 && !docked_)
            return;

        if (user_.x < 0)
            user_ = Rect{(ctx.layout.screenW - user_.w) * 0.5f, user_.y, user_.w, 46.0f};
        // More icons than fit on one line wrap onto extra lines; the panel grows upward by those lines
        // (bottom edge and HP bar stay put). Line count from the last frame (it only changes with the target).
        if (idx == 0)
            lines_ = 1;
        Rect dock         = ctx.layout.targetbar;
        const float extra = static_cast<float>(lines_ - 1) * (std::max(gui->GetTextLineHeight(), kIcon) + kLineGap);
        dock.y -= extra, dock.h += extra;
        auto p = mod::BeginPanel(ctx, gui, "##fpui_targetbar", nullptr, docked_, dock, user_, locked_, 0.82f, false);
        if (p.open)
        {
            if (idx == 0)
            {
                gui->TextDisabled("No target");
                mod::StripBar(ctx, gui, 0.0f, art::amber);
            }
            else if (object)
            {
                const char* nm = ent->GetName(idx);
                gui->TextColored(ToVec4(art::gold_hi), "%s", nm != nullptr && nm[0] != '\0' ? nm : "Object");
                mod::StripBar(ctx, gui, 0.0f, art::amber);
            }
            else
                RenderTarget(ctx, gui, idx);
        }
        mod::EndPanel(p, &user_, docked_, ctx);
        if (infoOpen_ && idx != 0 && !object)
            RenderInfo(ctx, gui);
    }

    // Mob info window (the "i" button on the target bar): everything the built-in monster data has on
    // the current target - levels, aggro / detection, respawn, weaknesses, immunities, drops at your
    // treasure hunter level and the steal list. Follows the target while it's open.
    void TargetBarModule::RenderInfo(Context& ctx, IGuiManager* gui)
    {
        auto* mm  = ctx.core->GetMemoryManager();
        auto* ent = mm->GetEntity();
        auto* tgt = mm->GetTarget();
        uint32_t idx = tgt->GetTargetIndex(tgt->GetIsSubTargetActive() != 0 ? 1u : 0u);
        if (idx == 0 || idx >= 2304 || ent->GetRawEntity(idx) == nullptr || (ent->GetSpawnFlags(idx) & 0x10u) == 0)
            return; // only monsters; stays open, shows again on the next one
        const char* name      = ent->GetName(idx);
        const MobInfo* info   = db_.Find(idx, name);
        const MobServer* srv  = db_.Server(ent->GetServerId(idx), name);
        const uint32_t sid    = ent->GetServerId(idx);
        const NmRecord* asPh  = db_.Placeholder(sid);
        const NmRecord* asNm  = db_.Notorious(sid);
        if (info == nullptr && srv == nullptr && asPh == nullptr && asNm == nullptr)
            return;
        const auto& t = ember::T();
        const std::string icons = ctx.resourceDir + "mobdb\\icons\\";
        auto* res = ctx.core->GetResourceManager();
        auto itemName = [&](int id) -> std::string {
            IItem* it = res->GetItemById(static_cast<uint32_t>(id));
            return it != nullptr && it->Name[0] != nullptr && it->Name[0][0] != '\0' ? it->Name[0] : "Item #" + std::to_string(id);
        };
        auto clock = [](int s) -> std::string {
            char b[32]{};
            if (s >= 3600)
                ::sprintf_s(b, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
            else
                ::sprintf_s(b, "%d:%02d", s / 60, s % 60);
            return b;
        };

        if (infoPos_.x < 0)
            infoPos_ = ImVec2(ctx.layout.targetbar.x, std::max(40.0f, ctx.layout.targetbar.y - 420.0f));
        gui->SetNextWindowPos(infoPos_, ImGuiCond_Appearing);
        gui->SetNextWindowSizeConstraints(ImVec2(320, 60), ImVec2(380, ctx.layout.screenH * 0.8f));
        gui->PushStyleColor(ImGuiCol_WindowBg, ToVec4(t.glass));
        gui->PushStyleColor(ImGuiCol_Border, ToVec4(t.edge));
        gui->PushStyleColor(ImGuiCol_Text, ToVec4(t.ivory));
        gui->PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        gui->PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
        if (gui->Begin("##fpui_mobinfo", &infoOpen_, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
        {
            infoPos_ = gui->GetWindowPos();
            const Rgba section = t.amber, label = t.muted, faint = t.dim;
            auto head = [&](const char* text) {
                gui->Dummy(ImVec2(0, 4));
                gui->TextColored(ToVec4(section), "%s", text);
                const ImVec2 p = gui->GetCursorScreenPos();
                ui::ImageAt(gui, ctx.white, p, ImVec2(gui->GetContentRegionAvail().x, 1), ImVec2(0, 0), ImVec2(1, 1), section, 0.18f);
                gui->Dummy(ImVec2(0, 3));
            };
            // Title: name, level, NM, close.
            const std::string title = srv != nullptr ? srv->name : (info != nullptr ? info->name : std::string(name != nullptr ? name : ""));
            gui->TextColored(ToVec4(t.ivory), "%s", title.c_str());
            if (info != nullptr && info->maxLevel > 0)
            {
                gui->SameLine(0.0f, 10.0f);
                if (info->minLevel == info->maxLevel)
                    gui->TextColored(ToVec4(t.amber), "Lv.%d", info->maxLevel);
                else
                    gui->TextColored(ToVec4(t.amber), "Lv.%d-%d", info->minLevel, info->maxLevel);
            }
            if ((info != nullptr && info->notorious) || (srv != nullptr && srv->notorious))
            {
                gui->SameLine(0.0f, 8.0f);
                gui->TextColored(ToVec4(t.ember), "NM");
            }
            gui->SameLine(std::max(gui->GetCursorPosX() + 8.0f, gui->GetWindowWidth() - 14.0f - 22.0f));
            if (gui->SmallButton("x##mobinfo"))
                infoOpen_ = false;

            // Behaviour.
            if (info != nullptr)
            {
                std::string how = info->aggro ? "Aggressive" : "Passive";
                if (info->link)
                    how += ", links";
                std::string detect;
                auto add = [&](bool on, const char* what) {
                    if (on)
                        detect += (detect.empty() ? "" : ", ") + std::string(what);
                };
                add(info->trueSight, "true sight");
                add(info->sight && !info->trueSight, "sight");
                add(info->sound, "sound");
                add(info->magic, "magic");
                add(info->scent, "scent");
                add(info->blood, "low HP");
                add(info->ja, "job abilities");
                gui->TextColored(ToVec4(info->aggro ? t.bad : t.ok), "%s", how.c_str());
                if (!detect.empty())
                {
                    gui->SameLine(0.0f, 6.0f);
                    gui->TextColored(ToVec4(label), "- detects %s", detect.c_str());
                }
            }
            // Respawn / spawn conditions.
            {
                int mn = srv != nullptr ? srv->respawnMin : (info != nullptr ? info->respawn : 0);
                int mx = srv != nullptr ? srv->respawnMax : mn;
                std::string rs = mn <= 0 ? "Scripted spawn" : mx > mn ? clock(mn) + " to " + clock(mx) : clock(mn);
                if (asNm != nullptr)
                    for (const auto& l : asNm->links)
                        if (l.type == "Placeholder" || l.type == "Lottery")
                            rs = "Via placeholder (see PH)";
                gui->TextColored(ToVec4(label), "Respawn");
                gui->SameLine(0.0f, 8.0f);
                gui->TextColored(ToVec4(t.ivory), "%s", rs.c_str());
                if (srv != nullptr && !srv->spawnTypes.empty())
                {
                    std::string st;
                    for (const auto& s : srv->spawnTypes)
                        st += (st.empty() ? "" : ", ") + s;
                    gui->SameLine(0.0f, 8.0f);
                    gui->TextColored(ToVec4(faint), "(%s)", st.c_str());
                }
            }
            // NM placeholder / lottery data.
            if (asPh != nullptr || asNm != nullptr)
            {
                const Rgba kPh = t.ok, kLottery = ember::Hex(0xf2c45aff), kLock = t.dim, kNm = t.ember;
                auto typeCol = [&](const std::string& ty) {
                    return ty == "Placeholder" ? kPh : ty == "Lottery" ? kLottery : ty == "Notorious" ? kNm : kLock;
                };
                // Strongest relationship as the type (a PH can feed several NMs).
                std::string kind = asNm != nullptr ? "Notorious Monster" : "Inactive";
                if (asNm == nullptr)
                {
                    const auto rank = [](const std::string& ty) { return ty == "Placeholder" ? 4 : ty == "Lottery" ? 3 : ty == "Lottery lock" ? 2 : 1; };
                    for (const auto& l : asPh->links)
                        if (rank(l.type) > rank(kind))
                            kind = l.type;
                }
                head("Notorious monster data");
                gui->TextColored(ToVec4(label), "NM type");
                gui->SameLine(0.0f, 8.0f);
                gui->TextColored(ToVec4(asNm != nullptr ? kNm : typeCol(kind)), "%s", kind.c_str());
                const NmRecord* rec = asNm != nullptr ? asNm : asPh;
                if (!MobDb::SameName(name, rec->name.c_str()))
                {
                    gui->SameLine(0.0f, 6.0f);
                    gui->TextColored(ToVec4(t.bad), "(unverified: the server data has %s here)", rec->name.c_str());
                }
                auto cooldown = [&](const NmLink& l) -> std::string {
                    auto fmt = [](int s) {
                        char b[16]{};
                        if (s >= 3600 && s % 3600 == 0)
                            ::sprintf_s(b, "%dh", s / 3600);
                        else if (s >= 3600)
                            ::sprintf_s(b, "%dh%02dm", s / 3600, s / 60 % 60);
                        else
                            ::sprintf_s(b, "%dm", s / 60);
                        return std::string(b);
                    };
                    if (l.cooldownMin <= 0)
                        return {};
                    return l.cooldownMax > l.cooldownMin ? fmt(l.cooldownMin) + "-" + fmt(l.cooldownMax) : fmt(l.cooldownMin);
                };
                gui->PushTextWrapPos(gui->GetCursorPosX() + 340.0f);
                if (asPh != nullptr)
                    for (const auto& l : asPh->links)
                    {
                        if (l.type == "Lottery lock" || l.type == "Inactive")
                        {
                            gui->TextColored(ToVec4(label), "Never pops");
                            gui->SameLine(0.0f, 8.0f);
                            gui->TextColored(ToVec4(kLock), "%s [%u] (%s)", l.name.c_str(), l.id & 0xFFF,
                                l.type == "Inactive" ? "listed, but never rolls" : "lottery lock only");
                            continue;
                        }
                        std::string d = l.name + " [" + std::to_string(l.id & 0xFFF) + "]";
                        if (l.chance > 0)
                        {
                            char c[16]{};
                            ::sprintf_s(c, ", %g%%", l.chance);
                            d += c;
                        }
                        if (const std::string cd = cooldown(l); !cd.empty() && l.type == "Placeholder")
                            d += ", " + cd + " window";
                        if (l.nightOnly)
                            d += ", night only";
                        else if (l.dayOnly)
                            d += ", day only";
                        if (l.status == "conditional")
                            d += ", conditional";
                        gui->TextColored(ToVec4(label), "Pops");
                        gui->SameLine(0.0f, 8.0f);
                        gui->TextColored(ToVec4(typeCol(l.type)), "%s", d.c_str());
                    }
                if (asNm != nullptr)
                    for (const auto& l : asNm->links)
                    {
                        // Up nearby? Only entities in the client's range are in memory.
                        const uint32_t pi = l.id & 0xFFF;
                        const bool up     = ent->GetServerId(pi) == l.id && ent->GetRawEntity(pi) != nullptr && (ent->GetRenderFlags0(pi) & 0x200u) != 0 &&
                                        ent->GetHPPercent(pi) > 0;
                        gui->TextColored(ToVec4(label), l.type == "Lottery lock" ? "Lock" : "PH");
                        gui->SameLine(0.0f, 8.0f);
                        if (up)
                            gui->TextColored(ToVec4(kPh), "%s [%u] %s - up %u%%", l.name.c_str(), pi, l.type.c_str(), static_cast<unsigned>(ent->GetHPPercent(pi)));
                        else
                            gui->TextColored(ToVec4(t.ivory), "%s [%u] %s", l.name.c_str(), pi, l.type.c_str());
                    }
                gui->PopTextWrapPos();
            }

            // Weak / resists.
            if (info != nullptr)
            {
                bool any = false;
                for (int i = 0; i < MobInfo::kModCount; ++i)
                    any |= std::abs(info->mods[i] - 1.0f) >= 0.01f;
                if (any)
                {
                    head("Damage taken");
                    int col = 0;
                    for (int i = 0; i < MobInfo::kModCount; ++i)
                    {
                        const float m = info->mods[i];
                        if (std::abs(m - 1.0f) < 0.01f)
                            continue;
                        if (col++ % 3 != 0)
                            gui->SameLine(0.0f, 14.0f);
                        if (IDirect3DTexture8* ic = ctx.textures->File(icons + MobInfo::kModNames[i] + ".png"); ic != nullptr)
                        {
                            const ImVec2 p = gui->GetCursorScreenPos();
                            ui::ImageAt(gui, ic, p, ImVec2(16, 16), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                            gui->Dummy(ImVec2(16, 16));
                            gui->SameLine(0.0f, 4.0f);
                        }
                        gui->TextColored(ToVec4(m > 1.0f ? kWeak : kResist), "%s %+d%%", MobInfo::kModNames[i], static_cast<int>(std::lround((m - 1.0f) * 100.0f)));
                    }
                }
                if (info->immunities != 0)
                {
                    head("Immune to");
                    std::string im;
                    for (const auto& x : kImmunities)
                        if ((info->immunities & x.flag) != 0)
                            im += (im.empty() ? "" : ", ") + std::string(x.label);
                    gui->PushTextWrapPos(gui->GetCursorPosX() + 340.0f);
                    gui->TextColored(ToVec4(t.ivory), "%s", im.c_str());
                    gui->PopTextWrapPos();
                }
            }
            // Drops (treasure hunter level selector) and steal.
            if (srv != nullptr)
            {
                head("Drops");
                gui->SameLine(std::max(gui->GetCursorPosX() + 8.0f, gui->GetWindowWidth() - 14.0f - 96.0f));
                if (gui->SmallButton("<##th"))
                    th_ = std::max(0, th_ - 1), ctx.MarkDirty();
                gui->SameLine(0.0f, 4.0f);
                gui->TextColored(ToVec4(t.amber), "TH %d", th_);
                gui->SameLine(0.0f, 4.0f);
                if (gui->SmallButton(">##th"))
                    th_ = std::min(14, th_ + 1), ctx.MarkDirty();
                const auto lines = db_.Drops(*srv, th_);
                if (lines.empty())
                    gui->TextColored(ToVec4(faint), "Nothing.");
                for (const auto& l : lines)
                {
                    gui->TextColored(ToVec4(t.ivory), "%s", itemName(l.item).c_str());
                    gui->SameLine(260.0f);
                    gui->TextColored(ToVec4(l.rate >= 100.0 ? t.ok : t.amber), "%s", l.text.c_str());
                }
                head(srv->stealAudit ? "Steal (unconfirmed)" : "Steal");
                if (srv->steal.empty())
                    gui->TextColored(ToVec4(faint), "Nothing to steal.");
                for (const int id : srv->steal)
                {
                    gui->TextColored(ToVec4(t.ivory), "%s", itemName(id).c_str());
                    if (srv->steal.size() > 1)
                    {
                        gui->SameLine(260.0f);
                        gui->TextColored(ToVec4(faint), "1 of %zu", srv->steal.size());
                    }
                }
            }
            else if (info != nullptr && !info->drops.empty())
            {
                head("Drops");
                for (const int id : info->drops)
                    gui->TextColored(ToVec4(t.ivory), "%s", itemName(id).c_str());
                gui->TextColored(ToVec4(faint), "(no server data here: rates and steal unknown)");
            }
        }
        gui->End();
        gui->PopStyleVar(3);
        gui->PopStyleColor(3);
    }

    // Row 1: name, level, NM | aggro/detection icons | weaknesses, resistances, immunities ... HP%  distance.
    // Row 2: HP bar.
    void TargetBarModule::RenderTarget(Context& ctx, IGuiManager* gui, uint32_t idx)
    {
        auto* mm    = ctx.core->GetMemoryManager();
        auto* ent   = mm->GetEntity();
        auto* party = mm->GetParty();
        const std::string install = InstallDir(ctx.core);
        db_.Load(ctx.resourceDir + "mobdb\\", party->GetMemberZone(0));
        const char* name    = ent->GetName(idx);
        const bool monster  = (ent->GetSpawnFlags(idx) & 0x10u) != 0;
        const MobInfo* info = monster && !slim_ ? db_.Find(idx, name) : nullptr; // slim: level / detection / resists are on the ring
        const std::string icons = ctx.resourceDir + "mobdb\\icons\\";
        (void)install;

        Rgba nameColor = art::gold_hi;
        if (monster)
        {
            const uint32_t claim = ent->GetClaimStatus(idx);
            if (claim != 0)
            {
                nameColor = kClaimedOther;
                for (uint32_t i = 0; i < 18; ++i)
                    if (party->GetMemberIsActive(i) != 0 && party->GetMemberServerId(i) == claim)
                        nameColor = kClaimedParty;
            }
        }
        const float width = gui->GetContentRegionAvail().x;
        const ImVec2 row  = gui->GetCursorScreenPos();
        const float lineH = gui->GetTextLineHeight();
        Flow r{gui, ctx, {}, 0.0f};
        r.Text(name != nullptr ? name : "", nameColor);
        if (info != nullptr && info->maxLevel > 0)
        {
            char lv[24]{};
            if (info->minLevel == info->maxLevel)
                ::sprintf_s(lv, "Lv.%d", info->maxLevel);
            else
                ::sprintf_s(lv, "Lv.%d-%d", info->minLevel, info->maxLevel);
            r.Text(lv, art::amber);
        }
        if (info != nullptr && info->notorious)
            r.Text("NM", art::ember);
        if (info != nullptr)
        {
            r.Gap(4.0f);
            r.Icon(icons + (info->aggro ? "AggroNQ.png" : "PassiveNQ.png"), info->aggro ? "Aggressive" : "Passive");
            if (info->link)
                r.Icon(icons + "Link.png", "Links");
            if (info->trueSight)
                r.Icon(icons + "TrueSight.png", "True sight");
            else if (info->sight)
                r.Icon(icons + "Sight.png", "Detects by sight");
            if (info->sound)
                r.Icon(icons + "Sound.png", "Detects by sound");
            if (info->scent)
                r.Icon(icons + "Scent.png", "Detects by scent");
            if (info->blood)
                r.Icon(icons + "Blood.png", "Detects low HP");
            if (info->magic)
                r.Icon(icons + "Magic.png", "Detects magic");
            if (info->ja)
                r.Icon(icons + "JA.png", "Detects job abilities");
            r.Gap(8.0f);
            // Damage taken: > 1 is a weakness, < 1 a resistance (e.g. 1.25 -> +25%, 0.875 -> -12%).
            for (int i = 0; i < MobInfo::kModCount; ++i)
            {
                const float m = info->mods[i];
                if (std::abs(m - 1.0f) < 0.01f || (m < 1.0f && !showResists_))
                    continue;
                char tip[64]{}, label[16]{};
                ::sprintf_s(tip, "%s: takes %d%% damage", MobInfo::kModNames[i], static_cast<int>(std::lround(m * 100.0f)));
                ::sprintf_s(label, "%+d%%", static_cast<int>(std::lround((m - 1.0f) * 100.0f)));
                r.Icon(icons + MobInfo::kModNames[i] + ".png", tip);
                r.Text(label, m > 1.0f ? kWeak : kResist, true); // icon and value stay together
            }
            if (showImmunities_ && info->immunities != 0)
            {
                r.Gap(4.0f);
                for (const auto& im : kImmunities)
                    if ((info->immunities & im.flag) != 0)
                    {
                        char tip[48]{};
                        ::sprintf_s(tip, "Immune: %s", im.label);
                        r.Icon(icons + im.icon + ".png", tip);
                    }
            }
        }
        // Right end: HP % and distance.
        const int pct = static_cast<int>(ent->GetHPPercent(idx));
        char right[48]{};
        ::sprintf_s(right, "%d%%   %.1f'", pct, static_cast<double>(std::sqrt(std::max(0.0f, ent->GetDistance(idx)))));
        float rw = gui->CalcTextSize(right).x;
        gui->SetCursorScreenPos(ImVec2(row.x + width - rw, row.y));
        gui->TextColored(ToVec4(art::gold_hi), "%s", right);
        // "i": mob info window, shown when the built-in data knows this monster.
        if (monster && (db_.Find(idx, name) != nullptr || db_.Server(ent->GetServerId(idx), name) != nullptr || db_.Placeholder(ent->GetServerId(idx)) != nullptr ||
                           db_.Notorious(ent->GetServerId(idx)) != nullptr))
        {
            const float bs = lineH + 2.0f;
            const ImVec2 bp(row.x + width - rw - bs - 8.0f, row.y - 1.0f);
            gui->SetCursorScreenPos(bp);
            const bool hit = gui->InvisibleButton("##mobinfo", ImVec2(bs, bs));
            const auto& et = ember::T();
            ember::RoundRect(gui, bp, ImVec2(bs, bs), infoOpen_ ? et.ember : (gui->IsItemHovered() ? Rgba{et.ember.r, et.ember.g, et.ember.b, 0.4f} : et.track), et.amber,
                bs * 0.5f, 1.0f);
            ember::TextCenter(gui, bp.x + bs * 0.5f, bp.y + (bs - lineH) * 0.5f, infoOpen_ ? et.ivory : et.amber, "i");
            if (gui->IsItemHovered())
                gui->SetTooltip("Monster info: drops, steal, respawn (/fpui targetbar info)");
            if (hit)
                infoOpen_ = !infoOpen_;
            rw += bs + 8.0f;
        }
        // Line 1 stops short of the HP% / distance; wrapped lines use the full width.
        lines_ = r.Layout(row, row.x + width - rw - 10.0f, row.x + width, lineH, true);
        const float rowsH = static_cast<float>(lines_ - 1) * (std::max(lineH, kIcon) + kLineGap);
        gui->SetCursorScreenPos(ImVec2(row.x, row.y + rowsH + lineH + 4.0f));

        const float hp = static_cast<float>(pct) / 100.0f;
        mod::StripBar(ctx, gui, hp, hp > 0.5f ? Rgba{0.55f, 0.82f, 0.62f, 1.0f} : hp > 0.25f ? art::amber : ctx.palette->danger);
    }
} // namespace farplaneui
