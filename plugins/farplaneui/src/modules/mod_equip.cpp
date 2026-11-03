/**
 * Farplane UI - equipment viewer: 4x4 grid of the 16 equipped slots with item icons.
 * Icons: IResourceManager::GetItemById(id)->Bitmap (a BMP from the item DAT) -> D3DX texture.
 */
#include "common.hpp"
#include "../ember.hpp"

namespace farplaneui
{
    namespace
    {
        // FFXI equipment menu order. Slot ids: 0 main 1 sub 2 range 3 ammo 4 head 5 body 6 hands 7 legs
        // 8 feet 9 neck 10 waist 11 ear1 12 ear2 13 ring1 14 ring2 15 back.
        constexpr uint32_t kGrid[16] = {0, 1, 2, 3, 4, 9, 11, 12, 5, 6, 13, 14, 15, 10, 7, 8};
        constexpr const char* kSlotNames[16] = {"Main", "Sub", "Range", "Ammo", "Head", "Body", "Hands", "Legs", "Feet", "Neck", "Waist",
            "Ear 1", "Ear 2", "Ring 1", "Ring 2", "Back"};
    } // namespace

    void EquipModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "equip", show_, docked_, locked_, user_);
        cell_ = std::clamp(cfg->GetFloat(alias, "equip", "cell", cell_), 20.0f, 80.0f);
    }
    void EquipModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "equip", show_, docked_, locked_, user_);
        ui::SetF(cfg, alias, "equip", "cell", cell_);
    }
    auto EquipModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (mod::DockCommand(ctx, "equip", args, show_, docked_, locked_))
            return true;
        float f = 0;
        if (ui::Lower(args[0]) == "size" && args.size() > 1 && ui::ParseFloat(args[1], f))
        {
            cell_ = std::clamp(f, 20.0f, 80.0f);
            ctx.MarkDirty();
            return true;
        }
        ctx.Print("/fpui equip [show|hide|dock|undock|lock|unlock] | size <20-80>");
        return true;
    }
    void EquipModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        if (g.Slider("Icon cell size", "Size of each slot when undocked.", cell_, 20.0f, 80.0f, "%.0f"))
            ctx.MarkDirty();
    }
    void EquipModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        auto* mm  = ctx.core->GetMemoryManager();
        auto* inv = mm != nullptr ? mm->GetInventory() : nullptr;
        auto* res = ctx.core->GetResourceManager();
        if (inv == nullptr || res == nullptr)
            return;
        auto p = mod::BeginPanel(ctx, gui, "##fpui_equip", nullptr, docked_, ctx.layout.equip, user_, locked_, 0.9f, true);
        if (p.open)
        {
            const ImVec2 avail = gui->GetContentRegionAvail();
            const float lineH  = gui->GetTextLineHeightWithSpacing(); // gil line under the grid
            const bool em      = ember::On();
            const auto& t      = ember::T();
            const float gap    = em ? t.S(t.cellGap) : 2.0f;
            const float gilH   = em ? t.S(t.gilH) + 2.0f : lineH;
            float cell         = docked_ ? std::max(16.0f, std::min(avail.x, avail.y - gilH) / 4.0f - gap) : cell_;
            if (em && docked_)
                cell = std::max(16.0f, std::min((avail.x - 3 * gap) / 4.0f, (avail.y - gilH - 3 * gap) / 4.0f));
            const ImVec2 o     = gui->GetCursorScreenPos();
            UINT rw = 0, rh = 0;
            IDirect3DTexture8* ring = ctx.Art("bar_trough_9slice", &rw, &rh);
            for (int k = 0; k < 16; ++k)
            {
                const uint32_t slot = kGrid[k];
                const ImVec2 pos(o.x + static_cast<float>(k % 4) * (cell + gap), o.y + static_cast<float>(k / 4) * (cell + gap));
                // Cell frame: art trough 9-slice, else a navy tile (Ember: rounded slot, drawn below once the item is known).
                if (em)
                    ;
                else if (ring != nullptr)
                    ui::NineSlice(gui, ring, rw, rh, pos, ImVec2(cell, cell), 5.0f, 4.0f, Rgba{1, 1, 1, 1});
                else if (ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, pos, ImVec2(cell, cell), ImVec2(0, 0), ImVec2(1, 1), art::navy, 0.9f);

                uint16_t itemId = 0;
                const auto* e   = inv->GetEquippedItem(slot);
                if (e != nullptr)
                {
                    const uint32_t container = (e->Index & 0xFF00u) >> 8;
                    const uint32_t index     = e->Index & 0xFFu;
                    if (index != 0) // index 0 = nothing equipped (slot 0 is the gil slot)
                    {
                        const auto* it = inv->GetContainerItem(container, index);
                        if (it != nullptr && it->Id != 0 && it->Id != 0xFFFF)
                            itemId = it->Id;
                    }
                }
                const char* name = nullptr;
                if (em)
                    ember::Slot(ctx, gui, pos, cell, itemId != 0);
                if (itemId != 0)
                {
                    IItem* item = res->GetItemById(itemId);
                    if (item != nullptr)
                    {
                        name = item->Name[0];
                        IDirect3DTexture8* icon = (item->ImageSize > 0 && item->ImageSize <= sizeof(item->Bitmap))
                                                      ? ctx.textures->Memory(itemId, item->Bitmap, item->ImageSize)
                                                      : nullptr;
                        const float in = em ? t.S(4) : 3.0f;
                        if (icon != nullptr)
                            ui::ImageAt(gui, icon, ImVec2(pos.x + in, pos.y + in), ImVec2(cell - 2 * in, cell - 2 * in), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                    }
                }
                gui->SetCursorScreenPos(pos);
                gui->PushID(k);
                gui->InvisibleButton("##slot", ImVec2(cell, cell));
                if (gui->IsItemHovered())
                    gui->SetTooltip("%s: %s", kSlotNames[slot], name != nullptr ? name : (itemId != 0 ? "?" : "(empty)"));
                gui->PopID();
            }
            // Gil, right-aligned under the grid (Ember: a centred pill across the grid).
            const float gridW = 4 * (cell + gap) - gap;
            const auto* gil   = inv->GetContainerItem(0, 0);
            std::string g     = gil != nullptr ? std::to_string(gil->Count) : std::string("-");
            for (int i = static_cast<int>(g.size()) - 3; i > 0 && g != "-"; i -= 3)
                g.insert(static_cast<size_t>(i), ",");
            g += " G";
            if (em)
            {
                const ImVec2 gp(o.x, o.y + 4 * (cell + gap) - gap + 2.0f);
                ember::Track(ctx, gui, gp, ImVec2(gridW, t.S(t.gilH)));
                ember::TextCenter(gui, gp.x + gridW * 0.5f, gp.y + (t.S(t.gilH) - gui->GetTextLineHeight()) * 0.5f, t.amber, g.c_str());
                gui->SetCursorScreenPos(ImVec2(o.x, gp.y + t.S(t.gilH)));
            }
            else
            {
                gui->SetCursorScreenPos(ImVec2(o.x + gridW - gui->CalcTextSize(g.c_str()).x, o.y + 4 * (cell + gap) + 2));
                gui->TextColored(ToVec4(art::gold_hi), "%s", g.c_str());
                gui->SetCursorScreenPos(ImVec2(o.x, o.y + 4 * (cell + gap) + lineH));
            }
            gui->Dummy(ImVec2(gridW, 0));
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }
} // namespace farplaneui
