/**
 * Farplane UI - event notifications (stack to the left of the character).
 *
 * Sources:
 *   XP / LP / CP     packet 0x02D, our own messages (8, 105, 253 XP; 371, 372 LP; 718, 735 CP)
 *   skill ups        packet 0x029, message 38 (+tenths) and 53 (new level); param1 = skill id
 *   treasure pool    packet 0x0D2 (item 0x10); quiet after zoning, when the pool is resent
 *   items / gil      count diff summed over every bag, so moving items between bags is silent
 *   key items, party invitations, trade requests    the game's system messages
 * Invitations and trade requests show the time they arrived (HH:MM) and close when answered: an
 * invitation when the inviter is in your party / alliance, a trade when the game sends a trade action
 * (0x022: the trade window opening or a cancel); unanswered ones expire (party 90s, trade 30s). Everything
 * else fades after its lifetime.
 */
#include "common.hpp"

#include <chrono>
#include <ctime>

namespace farplaneui
{
    namespace
    {
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
        auto Sep(uint64_t v) -> std::string
        {
            std::string s = std::to_string(v);
            for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
                s.insert(static_cast<size_t>(i), ",");
            return s;
        }
        auto SkillName(uint32_t id) -> const char*
        {
            switch (id)
            {
            case 1: return "Hand-to-Hand"; case 2: return "Dagger"; case 3: return "Sword"; case 4: return "Great Sword";
            case 5: return "Axe"; case 6: return "Great Axe"; case 7: return "Scythe"; case 8: return "Polearm";
            case 9: return "Katana"; case 10: return "Great Katana"; case 11: return "Club"; case 12: return "Staff";
            case 25: return "Archery"; case 26: return "Marksmanship"; case 27: return "Throwing"; case 28: return "Guard";
            case 29: return "Evasion"; case 30: return "Shield"; case 31: return "Parrying"; case 32: return "Divine Magic";
            case 33: return "Healing Magic"; case 34: return "Enhancing Magic"; case 35: return "Enfeebling Magic";
            case 36: return "Elemental Magic"; case 37: return "Dark Magic"; case 38: return "Summoning Magic"; case 39: return "Ninjutsu";
            case 40: return "Singing"; case 41: return "String"; case 42: return "Wind"; case 43: return "Blue Magic";
            case 44: return "Geomancy"; case 45: return "Handbell"; case 48: return "Fishing"; case 49: return "Woodworking";
            case 50: return "Smithing"; case 51: return "Goldsmithing"; case 52: return "Clothcraft"; case 53: return "Leathercraft";
            case 54: return "Bonecraft"; case 55: return "Alchemy"; case 56: return "Cooking"; case 57: return "Synergy";
            default: return "Skill";
            }
        }
        // Text after `key` up to the end, without a trailing period.
        auto After(const std::string& s, size_t pos) -> std::string
        {
            std::string r = s.substr(pos);
            while (!r.empty() && (r.back() == '.' || r.back() == ' ' || r.back() == '!'))
                r.pop_back();
            return r;
        }

        constexpr Rgba kXp{0.96f, 0.86f, 0.55f, 1};
        constexpr Rgba kItem{0.92f, 0.92f, 0.95f, 1};
        constexpr Rgba kSkill{0.55f, 0.75f, 1.0f, 1};
        constexpr Rgba kKey{0.95f, 0.80f, 0.45f, 1};
        constexpr Rgba kInvite{0.62f, 0.90f, 0.62f, 1};
        constexpr Rgba kTrade{0.95f, 0.65f, 0.45f, 1};
        constexpr uint32_t kBags = 17; // container ids 0..16
        auto ClockNow(void) -> std::string
        {
            const std::time_t t = std::time(nullptr);
            std::tm tm{};
            ::localtime_s(&tm, &t);
            char b[8]{};
            ::sprintf_s(b, "%02d:%02d", tm.tm_hour, tm.tm_min);
            return b;
        }
    } // namespace

    void NotifyModule::Push(Note n)
    {
        n.born = Now();
        if (n.countdown)
            n.clock = ClockNow();
        if (n.expires == 0)
            n.expires = n.born + lifetime_;
        notes_.push_back(std::move(n));
        while (static_cast<int>(notes_.size()) > maxRows_)
            notes_.pop_front();
    }

    void NotifyModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "notify";
        show_     = cfg->GetBool(alias, S, "show", show_);
        lifetime_ = std::clamp(cfg->GetFloat(alias, S, "lifetime", lifetime_), 2.0f, 30.0f);
        maxRows_  = std::clamp(cfg->GetInt32(alias, S, "rows", maxRows_), 3, 16);
        offsetX_  = cfg->GetFloat(alias, S, "offsetX", offsetX_);
        offsetY_  = cfg->GetFloat(alias, S, "offsetY", offsetY_);
        for (int i = 0; i < kTypes; ++i)
            on_[i] = cfg->GetBool(alias, S, kTypeKeys[i], on_[i]);
    }
    void NotifyModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "notify";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetF(cfg, alias, S, "lifetime", lifetime_);
        ui::SetI(cfg, alias, S, "rows", maxRows_);
        ui::SetF(cfg, alias, S, "offsetX", offsetX_);
        ui::SetF(cfg, alias, S, "offsetY", offsetY_);
        for (int i = 0; i < kTypes; ++i)
            ui::SetB(cfg, alias, S, kTypeKeys[i], on_[i]);
    }
    auto NotifyModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "test")
        {
            Push({"+125 XP", kXp, 0});
            Push({"+2 Fire Crystal", kItem, 4096});
            Push({"Sword +0.3", kSkill, 0});
            Push({"Key Item: Limit Breaker", kKey, 0});
            Push({"+250 gil", art::gold_hi, 0xFFFF});
            Push({"Treasure: Fire Crystal", kItem, 4096});
            Note inv{"Party Invitation: Retainer", kInvite, 0};
            inv.countdown = true, inv.expires = Now() + 90, inv.kind = 1, inv.who = "Retainer";
            Push(inv);
            Note tr{"Trade Request: Retainer", kTrade, 0};
            tr.countdown = true, tr.expires = Now() + 30, tr.kind = 2, tr.who = "Retainer";
            Push(tr);
            return true;
        }
        if (sub == "clear")
        {
            notes_.clear();
            return true;
        }
        if (sub == "show" || sub == "hide" || sub == "toggle")
        {
            show_ = sub == "toggle" ? !show_ : sub == "show";
            ctx.MarkDirty();
            return true;
        }
        ctx.Print("/fpui notify [show|hide] | test | clear");
        return true;
    }
    void NotifyModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Event notifications to the left of the character.", show_);
        ch |= g.Slider("Lifetime", "Seconds before a notification fades (invitations keep their own countdown).", lifetime_, 2.0f, 30.0f, "%.0fs");
        ch |= g.Slider("Maximum rows", "Most notifications shown at once.", maxRows_, 3, 16);
        ch |= g.Slider("Horizontal offset", "Move the stack left (-) or right (+) of its anchor.", offsetX_, -800.0f, 800.0f, "%.0f");
        ch |= g.Slider("Vertical offset", "Move the stack up (-) or down (+).", offsetY_, -600.0f, 600.0f, "%.0f");
        if (g.Button("Preview", "Show one of every notification type.", "Test"))
        {
            std::vector<std::string> a{"test"};
            Command(ctx, a);
        }
        g.Section("Events");
        for (int i = 0; i < kTypes; ++i)
            ch |= g.Toggle(kTypeTitles[i], kTypeDescs[i], on_[i]);
        if (ch)
            ctx.MarkDirty();
    }

    void NotifyModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        if (data == nullptr || party == nullptr)
            return;
        const uint32_t me = party->GetMemberServerId(0);
        if (id == 0x022) // trade action: the request was answered (window opened, or cancelled)
        {
            notes_.erase(std::remove_if(notes_.begin(), notes_.end(), [](const Note& n) { return n.kind == 2; }), notes_.end());
            return;
        }
        if (id == 0x00A) // zone in: bags and the treasure pool are resent
        {
            quietUntil_ = Now() + 10.0;
            return;
        }
        if (id == 0x02D && U32(data, size, 0x04) == me)
        {
            const uint32_t value = U32(data, size, 0x10), msg = U16(data, size, 0x18) % 1024;
            const char* unit = nullptr;
            if (msg == 8 || msg == 105 || msg == 253)
                unit = "XP";
            else if (msg == 371 || msg == 372)
                unit = "LP";
            else if (msg == 718 || msg == 735)
                unit = "CP";
            if (unit != nullptr && on_[kXpType] && value > 0)
                Push({"+" + Sep(value) + " " + unit, kXp, 0});
        }
        else if (id == 0x029 && on_[kSkillType] && U32(data, size, 0x04) == me)
        {
            const uint32_t msg = U16(data, size, 0x18) & 0x7FFF, skill = U32(data, size, 0x0C), value = U32(data, size, 0x10);
            char b[64]{};
            if (msg == 38)
                ::sprintf_s(b, "%s +%u.%u", SkillName(skill), value / 10, value % 10);
            else if (msg == 53)
                ::sprintf_s(b, "%s reaches %u", SkillName(skill), value);
            if (b[0] != '\0')
                Push({b, kSkill, 0});
        }
        else if (id == 0x0D2 && on_[kTreasureType] && Now() > quietUntil_)
        {
            const uint32_t item = U16(data, size, 0x10);
            if (item != 0 && item != 0xFFFF)
            {
                IItem* it = ctx.core->GetResourceManager()->GetItemById(item);
                Push({std::string("Treasure: ") + (it != nullptr ? it->Name[0] : "?"), kItem, static_cast<uint16_t>(item)});
            }
        }
    }

    auto NotifyModule::IncomingText(Context& ctx, int32_t, const char* message, bool) -> bool
    {
        if (message == nullptr || message[0] == '\0')
            return false;
        for (const auto& line : chat::Decode(ctx.core->GetChatManager(), message))
        {
            const std::string t = chat::PlainText(line);
            size_t p = 0;
            if (on_[kKeyType] && (p = t.find("Obtained key item: ")) != std::string::npos)
                Push({"Key Item: " + After(t, p + 19), kKey, 0});
            else if (on_[kInviteType] && (p = t.find(" invites you to")) != std::string::npos)
            {
                const bool alliance = t.find("alliance") != std::string::npos;
                Note n{std::string(alliance ? "Alliance Invitation: " : "Party Invitation: ") + t.substr(0, p), kInvite, 0};
                n.countdown = true, n.expires = Now() + 90.0, n.kind = 1, n.who = t.substr(0, p);
                Push(n);
            }
            else if (on_[kTradeType] && (p = t.find(" wishes to trade with you")) != std::string::npos)
            {
                Note n{"Trade Request: " + t.substr(0, p), kTrade, 0};
                n.countdown = true, n.expires = Now() + 30.0, n.kind = 2, n.who = t.substr(0, p);
                Push(n);
            }
        }
        return false;
    }

    // Item / gil changes: counts summed over every bag, checked twice a second.
    void NotifyModule::Frame(Context& ctx)
    {
        const double now = Now();
        // Expired ones go, wherever they are (invitations outlive the notes around them).
        notes_.erase(std::remove_if(notes_.begin(), notes_.end(), [&](const Note& n) { return now > n.expires; }), notes_.end());
        if (now - lastScan_ < 0.5)
            return;
        lastScan_ = now;
        // Accepted invitation: the inviter is now in your party / alliance.
        if (auto* party = ctx.core->GetMemoryManager() != nullptr ? ctx.core->GetMemoryManager()->GetParty() : nullptr; party != nullptr)
            notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                             [&](const Note& n) {
                                 if (n.kind != 1 || n.who.empty())
                                     return false;
                                 for (uint32_t s = 1; s < 18; ++s)
                                     if (party->GetMemberIsActive(s) != 0 && party->GetMemberName(s) != nullptr && _stricmp(party->GetMemberName(s), n.who.c_str()) == 0)
                                         return true;
                                 return false;
                             }),
                notes_.end());
        auto* inv = ctx.core->GetMemoryManager() != nullptr ? ctx.core->GetMemoryManager()->GetInventory() : nullptr;
        if (inv == nullptr)
            return;
        std::unordered_map<uint16_t, uint32_t> counts;
        uint32_t total = 0;
        for (uint32_t c = 0; c < kBags; ++c)
        {
            const uint32_t n = std::min<uint32_t>(inv->GetContainerCountMax(c), 80); // slots 1..max (slot 0: gil)
            for (uint32_t i = c == 0 ? 1u : 0u; i <= n; ++i)
            {
                const auto* it = inv->GetContainerItem(c, i);
                if (it != nullptr && it->Id != 0 && it->Id != 0xFFFF && it->Count > 0)
                    counts[it->Id] += it->Count, total += it->Count;
            }
        }
        const auto* gilItem = inv->GetContainerItem(0, 0);
        const uint32_t gil  = gilItem != nullptr ? gilItem->Count : 0;
        // Bags not loaded yet (login / zoning) or still settling: just take a new baseline.
        const bool quiet = now < quietUntil_ || total == 0 || !haveSnapshot_;
        if (!quiet)
        {
            if (on_[kGilType] && gil != lastGil_)
            {
                const int64_t d = static_cast<int64_t>(gil) - static_cast<int64_t>(lastGil_);
                Push({(d > 0 ? "+" : "-") + Sep(static_cast<uint64_t>(d > 0 ? d : -d)) + " gil", art::gold_hi, 0xFFFF});
            }
            if (on_[kItemType])
                for (const auto& [itemId, n] : counts)
                {
                    const auto old = counts_.find(itemId);
                    const uint32_t before = old != counts_.end() ? old->second : 0;
                    if (n > before)
                    {
                        IItem* it = ctx.core->GetResourceManager()->GetItemById(itemId);
                        Push({"+" + std::to_string(n - before) + " " + (it != nullptr ? it->Name[0] : "?"), kItem, itemId});
                    }
                }
        }
        counts_       = std::move(counts);
        lastGil_      = gil;
        haveSnapshot_ = total > 0;
    }

    // Right-aligned rows (text, then icon) ending just left of the character, newest at the bottom.
    void NotifyModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_ || notes_.empty())
            return;
        const float rowH = 22.0f, icon = 18.0f;
        const float right = ctx.layout.screenW * 0.5f - 150.0f + offsetX_;
        const float top   = ctx.layout.screenH * 0.42f + offsetY_;
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground;
        if (gui->Begin("##fpui_notify", nullptr, f))
        {
            const double now = Now();
            float y = top;
            for (const auto& n : notes_)
            {
                // Fade in over 0.2s, out over the last second.
                const float alpha = static_cast<float>(std::clamp(std::min((now - n.born) / 0.2, n.expires - now), 0.0, 1.0));
                std::string text = n.text;
                if (n.countdown && !n.clock.empty())
                    text += " (" + n.clock + ")";
                const float tw = gui->CalcTextSize(text.c_str()).x;
                float x = right - icon - 6.0f - tw;
                // Soft backing so the text reads on bright ground.
                ui::ImageAt(gui, ctx.hfade != nullptr ? ctx.hfade : ctx.white, ImVec2(x - 30.0f, y - 1), ImVec2(tw + icon + 42.0f, rowH - 2),
                    ImVec2(1, 0), ImVec2(0, 1), Rgba{0, 0, 0, 1}, 0.45f * alpha);
                gui->SetCursorScreenPos(ImVec2(x + 1, y + (rowH - gui->GetTextLineHeight()) * 0.5f + 1));
                gui->TextColored(ImVec4(0, 0, 0, 0.8f * alpha), "%s", text.c_str());
                gui->SetCursorScreenPos(ImVec2(x, y + (rowH - gui->GetTextLineHeight()) * 0.5f));
                gui->TextColored(ImVec4(n.color.r, n.color.g, n.color.b, alpha), "%s", text.c_str());
                if (n.itemId != 0)
                {
                    IItem* it = ctx.core->GetResourceManager()->GetItemById(n.itemId);
                    IDirect3DTexture8* t = (it != nullptr && it->ImageSize > 0 && it->ImageSize <= sizeof(it->Bitmap))
                                               ? ctx.textures->Memory(n.itemId, it->Bitmap, it->ImageSize)
                                               : nullptr;
                    if (t != nullptr)
                        ui::ImageAt(gui, t, ImVec2(right - icon, y + (rowH - icon) * 0.5f), ImVec2(icon, icon), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1}, alpha);
                }
                y += rowH;
            }
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }
} // namespace farplaneui
