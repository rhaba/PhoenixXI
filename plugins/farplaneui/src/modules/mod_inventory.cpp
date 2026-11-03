/**
 * Farplane UI - inventory tracker: used/max per selected bag with a progress bar.
 */
#include "common.hpp"
#include "../ember.hpp"

namespace farplaneui
{
    namespace
    {
        struct Bag
        {
            uint32_t id;
            const char* key;
            const char* title;
        };
        constexpr Bag kBags[] = {{0, "inventory", "Inventory"}, {1, "safe", "Mog Safe"}, {9, "safe2", "Mog Safe 2"}, {2, "storage", "Storage"},
            {4, "locker", "Locker"}, {5, "satchel", "Satchel"}, {6, "sack", "Sack"}, {7, "case", "Case"}, {8, "wardrobe", "Wardrobe"},
            {10, "wardrobe2", "Wardrobe 2"}, {11, "wardrobe3", "Wardrobe 3"}, {12, "wardrobe4", "Wardrobe 4"}, {13, "wardrobe5", "Wardrobe 5"},
            {14, "wardrobe6", "Wardrobe 6"}, {15, "wardrobe7", "Wardrobe 7"}, {16, "wardrobe8", "Wardrobe 8"}};
        // Bitmask is by container id (bit = 1 << id).
    } // namespace

    void InventoryModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "inventory", show_, docked_, locked_, user_);
        bags_ = cfg->GetUInt32(alias, "inventory", "bags", bags_);
    }
    void InventoryModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "inventory", show_, docked_, locked_, user_);
        char buf[16]{};
        ::sprintf_s(buf, "%u", bags_);
        cfg->SetValue(alias, "inventory", "bags", buf);
    }
    auto InventoryModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (mod::DockCommand(ctx, "inventory", args, show_, docked_, locked_))
            return true;
        const std::string sub = ui::Lower(args[0]);
        if ((sub == "bag" || sub == "bags") && args.size() > 1)
        {
            const std::string k = ui::Lower(args[1]);
            for (const auto& b : kBags)
            {
                if (k == b.key)
                {
                    bags_ ^= (1u << b.id);
                    ctx.Print(std::string(b.title) + ((bags_ & (1u << b.id)) != 0 ? " tracked." : " not tracked."));
                    ctx.MarkDirty();
                    return true;
                }
            }
        }
        std::string keys;
        for (const auto& b : kBags)
            keys += (keys.empty() ? "" : ", ") + std::string(b.key);
        ctx.Print("/fpui inventory [show|hide|dock|undock|lock|unlock] | bag <" + keys + "> (toggles)");
        return true;
    }
    void InventoryModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        g.Section("Tracked bags");
        for (const auto& b : kBags)
        {
            bool on = (bags_ & (1u << b.id)) != 0;
            if (g.Toggle(b.title, "Show this bag's used / total slots.", on))
            {
                bags_ = on ? (bags_ | (1u << b.id)) : (bags_ & ~(1u << b.id));
                ctx.MarkDirty();
            }
        }
    }
    void InventoryModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_ || bags_ == 0)
            return;
        auto* mm  = ctx.core->GetMemoryManager();
        auto* inv = mm != nullptr ? mm->GetInventory() : nullptr;
        if (inv == nullptr)
            return;
        Rect dock = ctx.layout.inventory;
        if (ember::On() && docked_)
        {
            // Stacked gauges: one row per tracked bag, the block anchored to the bottom of the DPS column.
            int n = 0;
            for (const auto& b : kBags)
                if ((bags_ & (1u << b.id)) != 0 && inv->GetContainerCountMax(b.id) > 0)
                    ++n;
            const auto& t   = ember::T();
            const float need = std::max(1, n) * t.S(t.invH) + std::max(0, n - 1) * t.S(t.invStackGap);
            dock.y += dock.h - need, dock.h = need;
        }
        auto p = mod::BeginPanel(ctx, gui, "##fpui_inventory", nullptr, docked_, dock, user_, locked_, 0.85f, false, ImGuiWindowFlags_NoScrollWithMouse); // slim one-line box: plain frame
        if (p.open)
        {
            // Numbers only, rows sitting on the bottom of the panel.
            struct Row
            {
                const char* title;
                uint32_t used, cap;
            };
            std::vector<Row> rows;
            for (const auto& b : kBags)
            {
                if ((bags_ & (1u << b.id)) == 0)
                    continue;
                const uint32_t slots = inv->GetContainerCountMax(b.id);
                if (slots == 0)
                    continue; // locked / not unlocked on this character
                // Slot 0 of every container is reserved (gil in the inventory); usable capacity is max - 1.
                uint32_t used = 0;
                for (uint32_t i = 1; i < slots; ++i)
                {
                    const auto* it = inv->GetContainerItem(b.id, i);
                    if (it != nullptr && it->Id != 0 && it->Id != 0xFFFF)
                        ++used;
                }
                rows.push_back({b.title, used, slots - 1});
            }
            // Ember: one gauge pill per bag, side by side (amber from 85% full, red when full).
            if (ember::On())
            {
                const auto& t   = ember::T();
                const ImVec2 o  = gui->GetWindowPos();
                const ImVec2 ws = gui->GetWindowSize();
                const float gw = ws.x, gh = t.S(t.invH); // stacked, full width
                gui->PushFont(nullptr, std::max(10.0f, t.S(11)));
                const float th = gui->GetTextLineHeight();
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    const auto& r   = rows[i];
                    const float f   = r.cap > 0 ? static_cast<float>(r.used) / static_cast<float>(r.cap) : 0.0f;
                    const Rgba& col = f >= t.fullAt ? t.bad : f >= t.warnAt ? t.amber : t.ok;
                    const ImVec2 p0(o.x, o.y + static_cast<float>(i) * (gh + t.S(t.invStackGap)));
                    ember::Track(ctx, gui, p0, ImVec2(gw, gh));
                    const float ty = p0.y + (gh - th) * 0.5f - t.S(2);
                    ember::Text(gui, ImVec2(p0.x + t.S(10), ty), t.muted, r.title);
                    char v[24]{};
                    ::sprintf_s(v, "%u/%u", r.used, r.cap);
                    ember::TextRight(gui, p0.x + gw - t.S(10), ty, col, v);
                    ember::Bar(ctx, gui, ImVec2(p0.x + t.S(10), p0.y + gh - t.S(4)), ImVec2(gw - t.S(20), std::max(1.0f, t.S(2))), f, col, ember::TrackKind::Faint);
                }
                gui->PopFont();
                gui->SetCursorScreenPos(o);
                gui->Dummy(ImVec2(0, 0));
                mod::EndPanel(p, &user_, docked_, ctx);
                return;
            }
            // One line along the bottom: "Inventory 20/29 - Satchel 29/29 - Wardrobe 24/24".
            const float spare = gui->GetContentRegionAvail().y - gui->GetTextLineHeight();
            if (spare > 0)
                gui->SetCursorPosY(gui->GetCursorPosY() + spare * (docked_ ? 0.5f : 1.0f));
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const auto& r = rows[i];
                if (i > 0)
                {
                    gui->SameLine(0.0f, 8.0f);
                    gui->TextColored(ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.4f}), "|");
                    gui->SameLine(0.0f, 8.0f);
                }
                gui->TextColored(ToVec4(art::amber), "%s", r.title);
                gui->SameLine(0.0f, 4.0f);
                gui->TextColored(ToVec4(r.used >= r.cap ? ctx.palette->danger : r.used * 100 >= r.cap * 85 ? art::ember : art::gold_hi), "%u/%u", r.used, r.cap);
            }
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }
} // namespace farplaneui
