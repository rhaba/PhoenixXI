/**
 * Farplane UI - DPS meter.
 *
 * Damage dealt by party / alliance members, read from action packets (0x028):
 *   actor @40 (32), category @82 (4); targets from bit 150: id (32), action count (4); each action:
 *   reaction 5, animation 12, effect 7, knockback 3, param 17, message 10, flags 31, then an optional
 *   additional effect (1 + 10 + 17 param + 10 message) and an optional spikes effect (1 + 10 + 14 + 10).
 * A param counts as damage when its message is one of the damage messages below; additional-effect
 * damage (enspells, skillchains) counts too, spikes (damage taken by the attacker) do not.
 *
 * The damage message list is derived from Parse (action_messages.lua, messages coloured "D" that
 * damage the target), used under its licence:
 *   Copyright 2016-2023 Flippant. Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met: 1. Redistributions of
 *   source code must retain the above copyright notice, this list of conditions and the following
 *   disclaimer. 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *   of conditions and the following disclaimer in the documentation and/or other materials provided
 *   with the distribution. 3. Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from this software without
 *   specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */
#include "common.hpp"
#include "../ember.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        constexpr uint16_t kDamageMessages[] = {1, 2, 67, 77, 110, 157, 163, 185, 196, 197, 223, 229, 252, 264, 265, 288, 289, 290, 291, 292, 293,
            294, 295, 296, 297, 298, 299, 300, 301, 302, 317, 352, 353, 379, 413, 522, 576, 577, 648, 650, 732, 747, 767, 768, 800};
        constexpr double kGapCap = 10.0; // idle gaps longer than this don't count toward DPS time

        auto IsDamage(uint32_t msg) -> bool
        {
            for (const auto m : kDamageMessages)
                if (m == msg)
                    return true;
            return false;
        }
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
        // "12.3k" / "1.2M"
        auto Short(double v) -> std::string
        {
            char b[24]{};
            if (v >= 1e6)
                ::sprintf_s(b, "%.1fM", v / 1e6);
            else if (v >= 1e4)
                ::sprintf_s(b, "%.1fk", v / 1e3);
            else
                ::sprintf_s(b, "%.0f", v);
            return b;
        }
        constexpr const char* kCatNames[DpsModule::kCats] = {"Melee", "Ranged", "Weapon skill", "Magic", "Abilities", "Extra"};
    } // namespace

    void DpsModule::Add(uint32_t actor, const std::string& name, int cat, uint32_t dmg)
    {
        if (dmg == 0)
            return;
        const double now = Now();
        auto& s = stats_[actor];
        s.name = name;
        if (s.last > 0)
            s.active += std::min(now - s.last, kGapCap);
        else
            s.active += 1.0; // first hit counts as one second
        s.last = now;
        s.total += dmg;
        s.cats[cat] += dmg;
        if (startedAt_ == 0)
            startedAt_ = now;
        lastHitAt_ = now;
    }

    void DpsModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr || paused_)
            return;
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        const uint32_t actor = Bits(data, size, 40, 32);
        const char* name     = nullptr;
        for (uint32_t s = 0; s < 18 && name == nullptr; ++s)
            if (party->GetMemberIsActive(s) != 0 && party->GetMemberServerId(s) == actor)
                name = party->GetMemberName(s);
        if (name == nullptr || name[0] == '\0')
            return;
        const uint32_t category = Bits(data, size, 82, 4);
        int cat = -1;
        switch (category)
        {
        case 1: cat = 0; break;            // melee round
        case 2: cat = 1; break;            // ranged finish
        case 3: case 11: cat = 2; break;   // weapon skill (11: monster-style skills, e.g. some pets/avatars)
        case 4: cat = 3; break;            // spell finish
        case 6: case 14: case 15: cat = 4; break; // job abilities, dances, rune fencer
        default: return;
        }
        const uint32_t targets = Bits(data, size, 72, 6);
        uint32_t pos = 150;
        for (uint32_t t = 0; t < targets && pos / 8 < size; ++t)
        {
            pos += 32; // target id
            const uint32_t actions = Bits(data, size, pos, 4);
            pos += 4;
            for (uint32_t a = 0; a < actions && pos / 8 < size; ++a)
            {
                pos += 5 + 12 + 7 + 3;
                const uint32_t param = Bits(data, size, pos, 17);
                pos += 17;
                const uint32_t msg = Bits(data, size, pos, 10);
                pos += 10 + 31;
                if (IsDamage(msg))
                    Add(actor, name, cat, param);
                if (Bits(data, size, pos++, 1) != 0) // additional effect
                {
                    pos += 10;
                    const uint32_t ap = Bits(data, size, pos, 17);
                    pos += 17;
                    const uint32_t am = Bits(data, size, pos, 10);
                    pos += 10;
                    if (IsDamage(am))
                        Add(actor, name, 5, ap);
                }
                if (Bits(data, size, pos++, 1) != 0) // spikes: damage to the attacker, not counted
                    pos += 10 + 14 + 10;
            }
        }
    }

    void DpsModule::Report(Context& ctx, const std::string& channel)
    {
        std::vector<const Stat*> rows;
        uint64_t total = 0;
        for (const auto& [id, s] : stats_)
            rows.push_back(&s), total += s.total;
        std::sort(rows.begin(), rows.end(), [](const Stat* a, const Stat* b) { return a->total > b->total; });
        if (rows.empty())
        {
            ctx.Print("DPS: no damage recorded yet.");
            return;
        }
        std::string line = "DPS:";
        for (size_t i = 0; i < rows.size() && i < 6; ++i)
        {
            const auto* s = rows[i];
            char b[96]{};
            ::sprintf_s(b, " %s %s (%.0f%%, %.0f/s)%s", s->name.c_str(), Short(static_cast<double>(s->total)).c_str(),
                total > 0 ? 100.0 * s->total / total : 0.0, s->total / std::max(1.0, s->active), i + 1 < rows.size() && i < 5 ? "," : "");
            line += b;
        }
        if (channel.empty())
            ctx.Print(line);
        else if (auto* chat = ctx.core->GetChatManager(); chat != nullptr)
            chat->QueueCommand(-1, ("/" + channel + " " + line).c_str());
    }

    void DpsModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        const double now = Now();
        std::vector<Stat> rows;
        if (now < testUntil_)
        {
            const char* me = ctx.core->GetMemoryManager()->GetParty()->GetMemberName(0);
            rows = {{me != nullptr ? me : "You", 48210, {30110, 0, 14200, 0, 1900, 2000}, 410, 0},
                {"Kelz", 61880, {42000, 0, 18000, 0, 880, 1000}, 400, 0},
                {"Aeris", 39500, {0, 0, 0, 39500, 0, 0}, 380, 0},
                {"Tarru", 22140, {15100, 3040, 4000, 0, 0, 0}, 395, 0}};
        }
        else
            for (const auto& [id, s] : stats_)
                rows.push_back(s);
        if (rows.empty() && hideEmpty_ && !docked_)
            return; // docked, the box stays as part of the mask
        std::sort(rows.begin(), rows.end(), [](const Stat& a, const Stat& b) { return a.total > b.total; });
        uint64_t total = 0, top = 1;
        for (const auto& s : rows)
            total += s.total, top = std::max<uint64_t>(top, s.total);

        if (user_.x < 0)
            user_ = Rect{18.0f, ctx.layout.screenH * 0.42f, 280.0f, 60.0f};
        auto p = mod::BeginPanel(ctx, gui, "##fpui_dps", nullptr, docked_, ctx.layout.dps, user_, locked_, 0.85f, docked_,
            docked_ ? ImGuiWindowFlags_NoScrollWithMouse : ImGuiWindowFlags_AlwaysAutoResize);
        if (p.open && ember::On() && docked_)
        {
            // Ember: header + pill, rows with bars scaled to the top damage, own row glowing.
            const auto& t   = ember::T();
            const ImVec2 o  = gui->GetWindowPos();
            const float w   = ctx.layout.dps.w;
            const float ih  = ctx.layout.dps.h;
            const float rowH = t.S(t.dpsRowH);
            const float textH = gui->GetTextLineHeight();
            ember::Text(gui, o, t.amber, paused_ ? "DPS (paused)" : "DPS");
            const double elapsed = startedAt_ > 0 ? lastHitAt_ - startedAt_ : 0.0;
            char head[48]{};
            ::sprintf_s(head, "%s   %d:%02d", Short(static_cast<double>(total)).c_str(), static_cast<int>(elapsed) / 60, static_cast<int>(elapsed) % 60);
            const float hw = gui->CalcTextSize(head).x + t.S(20), hh = t.S(t.dpsHeaderH);
            ember::Track(ctx, gui, ImVec2(o.x + w - hw, o.y), ImVec2(hw, hh));
            ember::TextCenter(gui, o.x + w - hw * 0.5f, o.y + (hh - textH) * 0.5f, t.amber, head);
            const int maxRows = std::max(1, static_cast<int>((ih - t.S(24) - ctx.layout.inventory.h - t.S(6)) / rowH));
            const char* me    = ctx.core->GetMemoryManager()->GetParty()->GetMemberName(0);
            float y = o.y + t.S(24);
            if (rows.empty())
                ember::Text(gui, ImVec2(o.x, y), t.dim, "No damage yet.");
            for (size_t i = 0; i < rows.size() && static_cast<int>(i) < maxRows; ++i, y += rowH)
            {
                const auto& s    = rows[i];
                const bool self  = me != nullptr && s.name == me;
                if (self)
                    ember::Pill(ctx, gui, ImVec2(o.x - 4, y - 2), ImVec2(w + 8, rowH), t.ember, 55.0f / 255.0f);
                ember::Text(gui, ImVec2(o.x, y), self ? t.amber : t.ivory, s.name.c_str());
                const double dps = s.total / std::max(1.0, s.active);
                char a[24]{}, b[16]{}, c[16]{};
                ::sprintf_s(a, "%s", Short(static_cast<double>(s.total)).c_str());
                ::sprintf_s(b, "%.0f%%", total > 0 ? 100.0 * s.total / total : 0.0);
                ::sprintf_s(c, "%.0f/s", dps);
                const bool wide = w >= t.S(300); // per-second column only when there's room (it's in the tooltip)
                ember::TextRight(gui, o.x + w - t.S(wide ? 92.0f : 40.0f), y, t.ivory, a);
                ember::TextRight(gui, o.x + w - (wide ? t.S(46) : 0.0f), y, t.muted, b);
                if (wide)
                    ember::TextRight(gui, o.x + w, y, t.dim, c);
                if (top > 0)
                    ember::Bar(ctx, gui, ImVec2(o.x, y + t.S(14)), ImVec2(w * static_cast<float>(s.total) / static_cast<float>(top), std::max(2.0f, t.S(3))), 1.0f,
                        t.ember, ember::TrackKind::None, self ? 1.0f : 200.0f / 255.0f);
                const ImVec2 m = gui->GetIO().MousePos;
                if (m.x >= o.x && m.y >= y && m.x < o.x + w && m.y < y + rowH)
                {
                    std::string tip = s.name + "  " + c + "\n";
                    for (int k = 0; k < kCats; ++k)
                        if (s.cats[k] > 0)
                        {
                            char bb[64]{};
                            ::sprintf_s(bb, "%s: %s (%.0f%%)\n", kCatNames[k], Short(static_cast<double>(s.cats[k])).c_str(), 100.0 * s.cats[k] / std::max<uint64_t>(1, s.total));
                            tip += bb;
                        }
                    gui->SetTooltip("%s", tip.c_str());
                }
            }
            gui->SetCursorScreenPos(o);
            gui->Dummy(ImVec2(0, 0));
        }
        else if (p.open)
        {
            const float lineH = gui->GetTextLineHeight();
            float w = 260.0f;
            for (const auto& s : rows)
                w = std::max(w, gui->CalcTextSize(s.name.c_str()).x + 16.0f + gui->CalcTextSize("000.0k  100%  0000/s").x);
            if (docked_)
                w = gui->GetContentRegionAvail().x; // fill the docked box
            // Header: DPS ..... total  elapsed
            gui->TextColored(ToVec4(art::gold_hi), "DPS");
            if (paused_)
            {
                gui->SameLine();
                gui->TextColored(ToVec4(art::ember), "(paused)");
            }
            const double elapsed = startedAt_ > 0 ? lastHitAt_ - startedAt_ : 0.0;
            char head[48]{};
            ::sprintf_s(head, "%s  %d:%02d", Short(static_cast<double>(total)).c_str(), static_cast<int>(elapsed) / 60, static_cast<int>(elapsed) % 60);
            gui->SameLine(w - gui->CalcTextSize(head).x);
            gui->TextColored(ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.8f}), "%s", head);
            if (rows.empty())
                gui->TextDisabled("No damage yet.");
            for (const auto& s : rows)
            {
                const ImVec2 top0 = gui->GetCursorScreenPos();
                const double dps  = s.total / std::max(1.0, s.active);
                gui->TextColored(ToVec4(Rgba{0.96f, 0.95f, 0.9f, 1}), "%s", s.name.c_str());
                char right[64]{};
                ::sprintf_s(right, "%s  %.0f%%  %.0f/s", Short(static_cast<double>(s.total)).c_str(), total > 0 ? 100.0 * s.total / total : 0.0, dps);
                gui->SameLine(w - gui->CalcTextSize(right).x);
                gui->TextColored(ToVec4(art::gold_hi), "%s", right);
                const ImVec2 bp = gui->GetCursorScreenPos();
                mod::DrawBar(ctx, gui, bp, ImVec2(w, mod::kStripBarH), static_cast<float>(s.total) / static_cast<float>(top), art::ember, 1.0f, true);
                gui->Dummy(ImVec2(w, mod::kStripBarH + 4));
                // Breakdown on hover.
                const ImVec2 m = gui->GetIO().MousePos;
                if (m.x >= top0.x && m.y >= top0.y && m.x < top0.x + w && m.y < top0.y + lineH + mod::kStripBarH + 4)
                {
                    std::string tip = s.name + "\n";
                    for (int c = 0; c < kCats; ++c)
                        if (s.cats[c] > 0)
                        {
                            char b[64]{};
                            ::sprintf_s(b, "%s: %s (%.0f%%)\n", kCatNames[c], Short(static_cast<double>(s.cats[c])).c_str(), 100.0 * s.cats[c] / std::max<uint64_t>(1, s.total));
                            tip += b;
                        }
                    gui->SetTooltip("%s", tip.c_str());
                }
            }
            gui->Dummy(ImVec2(w, 0));
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }

    void DpsModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        show_      = cfg->GetBool(alias, "dps", "show", show_);
        docked_    = cfg->GetBool(alias, "dps", "docked", docked_);
        locked_    = cfg->GetBool(alias, "dps", "locked", locked_);
        hideEmpty_ = cfg->GetBool(alias, "dps", "hideEmpty", hideEmpty_);
        ui::LoadRect(cfg, alias, "dps", "win", user_);
    }
    void DpsModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        ui::SetB(cfg, alias, "dps", "show", show_);
        ui::SetB(cfg, alias, "dps", "docked", docked_);
        ui::SetB(cfg, alias, "dps", "locked", locked_);
        ui::SetB(cfg, alias, "dps", "hideEmpty", hideEmpty_);
        ui::SaveRect(cfg, alias, "dps", "win", user_);
    }
    auto DpsModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
            show_ = sub == "toggle" ? !show_ : sub == "show";
        else if (sub == "lock" || sub == "unlock")
            locked_ = sub == "lock";
        else if (sub == "dock" || sub == "undock")
            docked_ = sub == "dock";
        else if (sub == "reset")
        {
            stats_.clear();
            startedAt_ = lastHitAt_ = 0;
            ctx.Print("DPS: reset.");
            return true;
        }
        else if (sub == "pause")
        {
            paused_ = !paused_;
            ctx.Print(paused_ ? "DPS: paused." : "DPS: resumed.");
            return true;
        }
        else if (sub == "report")
        {
            const std::string ch = args.size() > 1 ? ui::Lower(args[1]) : std::string();
            Report(ctx, ch == "p" || ch == "party" ? "p" : ch == "l" || ch == "ls" ? "l" : ch == "l2" ? "l2" : "");
            return true;
        }
        else if (sub == "test")
        {
            testUntil_ = Now() + 15.0;
            return true;
        }
        else
        {
            ctx.Print("/fpui dps [show|hide|lock|unlock] | reset | pause | report [p|l|l2] | test");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }
    void DpsModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "Damage meter for your party and alliance.", show_);
        ch |= g.Toggle("Docked", "In its box in the mask, beside the minimap. Off: free-floating (drag to move).", docked_);
        if (!docked_)
            ch |= g.Toggle("Locked", "Stop the meter being dragged.", locked_);
        ch |= g.Toggle("Hide when empty", "Only show once damage has been recorded.", hideEmpty_);
        if (g.Button("Reset", "Clear all recorded damage.", "Reset"))
        {
            stats_.clear();
            startedAt_ = lastHitAt_ = 0;
        }
        if (g.Button("Preview", "Show sample rows for 15 seconds.", "Test"))
            testUntil_ = Now() + 15.0;
        g.Info("Credits", "Damage message list from Parse (Flippant, BSD licence).");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
