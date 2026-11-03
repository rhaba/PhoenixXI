/**
 * Farplane UI - timers, drawn like the tTimers "classic" renderer with the Farplane IX skin
 * (tTimers: MIT, Thorny; party fork and Farplane IX skin: Spongeh). Two groups, each its own stack:
 *
 *   Recast    your job abilities (IRecast slots 0..31; slot 0 = one-hour) and spells, in 1/60 s.
 *             Ready / Quick Draw / Stratagems show charges, from the recast table's per-slot modifier.
 *   Party     party members' job abilities: each use (action categories 6, 14, 15) carries the recast
 *             the server applied, merits and gear included; when it doesn't (Blood Pacts) the base
 *             recast comes from data/party_recasts.hpp. Charge abilities stack uses.
 *
 * Rows: outline + tinted bar (green -> amber -> red by time left), icon, name, time on the right.
 * Bottom-justified groups grow upward from their anchor. A finished timer flashes its full bar for a
 * few seconds, then leaves. Sorted soonest first ("Nominal").
 */
#include "common.hpp"
#include "../data/party_recasts.hpp"

#include <chrono>

namespace farplaneui
{
    namespace
    {
        auto Now(void) -> double
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
        // tTimers TimeToString: h:mm, m:ss, then seconds with tenths.
        auto TimeText(double t, bool tenths) -> std::string
        {
            char b[24]{};
            if (t >= 3600.0)
                ::sprintf_s(b, "%d:%02d", static_cast<int>(t / 3600), static_cast<int>(std::fmod(t, 3600.0) / 60));
            else if (t >= 60.0)
                ::sprintf_s(b, "%d:%02d", static_cast<int>(t / 60), static_cast<int>(std::fmod(t, 60.0)));
            else if (tenths)
                ::sprintf_s(b, "%d.%d", static_cast<int>(t), static_cast<int>(std::fmod(t, 1.0) * 10));
            else
                ::sprintf_s(b, "%d", static_cast<int>(t));
            return b;
        }
        auto ChargeText(double t) -> std::string
        {
            if (t <= 0)
                return "Ready";
            char b[24]{};
            if (t < 60)
                ::sprintf_s(b, "%d.%d", static_cast<int>(t), static_cast<int>(std::fmod(t * 10, 10.0)));
            else
                ::sprintf_s(b, "%d:%02d", static_cast<int>(t / 60), static_cast<int>(std::fmod(t, 60.0)));
            return b;
        }
        // Least-significant-bit-first bit field from an action packet.
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
        auto BaseRecast(uint32_t ability) -> const data::PartyRecast*
        {
            for (const auto& r : data::kPartyRecasts)
                if (r.ability == ability)
                    return &r;
            return nullptr;
        }
        auto IsCharge(uint32_t recastId) -> bool { return recastId == 102 || recastId == 195 || recastId == 231; } // Ready, Quick Draw, Stratagems

        constexpr const char* kOneHour[] = {"One-Hour Ability", "Mighty Strikes", "Hundred Fists", "Benediction", "Manafont", "Chainspell",
            "Perfect Dodge", "Invincible", "Blood Weapon", "Familiar", "Soul Voice", "Eagle Eye Shot", "Meikyo Shisui", "Mijin Gakure", "Spirit Surge",
            "Astral Flow", "Azure Lore", "Wild Card", "Overdrive", "Trance", "Tabula Rasa", "Bolster", "Elemental Sforzo"};
        constexpr const char* kOneHour2[] = {"Secondary One-Hour", "Brazen Rush", "Inner Strength", "Asylum", "Subtle Sorcery", "Stymie", "Larceny",
            "Intervene", "Soul Enslavement", "Unleash", "Clarion Call", "Overkill", "Yaegasumi", "Mikage", "Fly High", "Astral Conduit",
            "Unbridled Wisdom", "Cutting Cards", "Heady Artifice", "Grand Pas", "Caper Emissarius", "Widened Compass", "Odyllic Subterfuge"};

        // Farplane IX skin (tTimers skins/classic/farplane9.lua), sizes at scale 1.
        constexpr float kBarW = 215, kBarH = 16, kStep = 17, kNameX = 20, kTextY = 1.5f, kTimerX = 6, kIcon = 16, kFont = 11, kHandle = 40;
        constexpr double kLow = 10, kMid = 30, kHigh = 60; // seconds
        constexpr Rgba kColLow{1.0f, 0x5A / 255.0f, 0x3A / 255.0f, 1}, kColMid{1.0f, 0xD2 / 255.0f, 0x7A / 255.0f, 1},
            kColHigh{0x8F / 255.0f, 0xD6 / 255.0f, 0xA0 / 255.0f, 1};
        constexpr Rgba kText{1.0f, 0xF4 / 255.0f, 0xE8 / 255.0f, 1}, kOutline{0x14 / 255.0f, 0x13 / 255.0f, 0x17 / 255.0f, 1};
        constexpr Rgba kHandleFill{0xD2 / 255.0f, 0x64 / 255.0f, 0x2A / 255.0f, 0.75f};

        auto Mix(const Rgba& a, const Rgba& b, float t) -> Rgba
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1};
        }
        // Recast colouring (tTimers classic, not reversed): nearly ready = green, long wait = red.
        auto BarColor(double left, bool reverse) -> Rgba
        {
            const Rgba& ready = reverse ? kColLow : kColHigh;
            const Rgba& wait  = reverse ? kColHigh : kColLow;
            if (left < kLow)
                return ready;
            if (left < kMid)
                return Mix(ready, kColMid, static_cast<float>((left - kLow) / (kMid - kLow)));
            if (left < kHigh)
                return Mix(kColMid, wait, static_cast<float>((left - kMid) / (kHigh - kMid)));
            return wait;
        }
    } // namespace

    auto RecastModule::AbilityName(Context& ctx, uint32_t slot, uint32_t timerId) -> std::string
    {
        auto* player       = ctx.core->GetMemoryManager()->GetPlayer();
        const uint32_t job = player != nullptr ? player->GetMainJob() : 0;
        if (slot == 0)
            return kOneHour[job < std::size(kOneHour) ? job : 0];
        if (timerId == 254)
            return kOneHour2[job < std::size(kOneHour2) ? job : 0];
        if (timerId == 10)
            return "Rune Enchantment";
        if (timerId == 210)
            return "Maneuvers";
        if (auto it = abilityNames_.find(timerId); it != abilityNames_.end())
            return it->second;
        auto* res = ctx.core->GetResourceManager();
        std::string name;
        if (IAbility* a = res->GetAbilityByTimerId(timerId); a != nullptr && a->Name[0] != nullptr && a->Name[0][0] != '\0')
            name = a->Name[0];
        else
        {
            for (uint32_t id = 0x200; id < 0x700 && name.empty(); ++id)
                if (IAbility* b = res->GetAbilityById(id); b != nullptr && b->RecastTimerId == timerId && b->Name[0] != nullptr)
                    name = b->Name[0];
        }
        if (name.empty())
            name = "Unknown Ability [" + std::to_string(timerId) + "]";
        abilityNames_[timerId] = name;
        return name;
    }

    // Icon from resources\farplaneui\ttimers\<dir>\<id>_<job>.png, else <id>.png (missing files are
    // remembered as missing by the texture cache).
    auto RecastModule::Icon(Context& ctx, const char* dir, uint32_t id, uint32_t job) -> IDirect3DTexture8*
    {
        if (ctx.textures == nullptr)
            return nullptr;
        const std::string base = ctx.resourceDir + "ttimers\\" + dir + "\\" + std::to_string(id);
        if (job != 0)
            if (IDirect3DTexture8* t = ctx.textures->File(base + "_" + std::to_string(job) + ".png"); t != nullptr)
                return t;
        return ctx.textures->File(base + ".png");
    }

    // Recast table base (tTimers: '894124E9????????8B46??6A006A00508BCEE8' + 0x19, deref): per slot
    // 8 bytes, int16 recast modifier at +4 (merits / gear), used for charge maths.
    auto RecastModule::ChargeModifier(uint32_t slot) -> int
    {
        if (!recastSigTried_)
        {
            recastSigTried_ = true;
            if (const HMODULE m = ::GetModuleHandleA("FFXiMain.dll"); m != nullptr)
            {
                const auto base = reinterpret_cast<uintptr_t>(m);
                const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(m)->e_lfanew);
                const uintptr_t p = Ashita::Memory::FindPattern(base, nt->OptionalHeader.SizeOfImage, "894124E9????????8B46??6A006A00508BCEE8", 0x19, 0);
                recastBase_ = p != 0 ? Ashita::Memory::SafeRead<uint32_t>(p, 0u) : 0u;
            }
        }
        return recastBase_ != 0 ? Ashita::Memory::SafeRead<int16_t>(recastBase_ + slot * 8 + 4, static_cast<int16_t>(0)) : 0;
    }

    void RecastModule::Direct3DInit(Context& ctx)
    {
        font_ = nullptr;
        const std::string path = ctx.resourceDir + "fonts\\Play-Regular.ttf";
        if (::GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
            return;
        if (auto* gui = ctx.core->GetGuiManager(); gui != nullptr)
            font_ = gui->AddFontFromFileTTF(path.c_str(), 16.0f, nullptr);
    }

    // Party job abilities: action packet, actor at bit 40, category at 82, ability id (17 bits) at 86,
    // recast in seconds (32 bits) at 118.
    void RecastModule::IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data)
    {
        if (id != 0x028 || data == nullptr || !party_)
            return;
        const uint32_t category = Bits(data, size, 82, 4);
        if (category != 6 && category != 14 && category != 15)
            return;
        auto* party = ctx.core->GetMemoryManager()->GetParty();
        const uint32_t actor = Bits(data, size, 40, 32);
        const char* member   = nullptr;
        uint32_t job         = 0;
        for (uint32_t slot = 0; slot < (partyAlliance_ ? 18u : 6u) && member == nullptr; ++slot)
            if (party->GetMemberIsActive(slot) != 0 && party->GetMemberServerId(slot) == actor && (slot != 0 || partySelf_))
                member = party->GetMemberName(slot), job = party->GetMemberMainJob(slot);
        if (member == nullptr)
            return;
        uint32_t ability = Bits(data, size, 86, 17);
        if (ability >= 0x200 && ability < 0x400)
            ability -= 0x200;
        const auto* base        = BaseRecast(ability);
        const uint32_t recastId = base != nullptr ? base->recastId : 0x10000u + ability;
        double duration         = static_cast<double>(Bits(data, size, 118, 32));
        const bool fromPacket   = duration > 0;
        if (!fromPacket && base != nullptr)
            duration = base->seconds;
        if (duration <= 0)
            return;
        const double now   = Now();
        const uint64_t key = (static_cast<uint64_t>(actor) << 32) | recastId;
        auto& t            = partyTimers_[key];
        double expires     = now + duration;
        if (IsCharge(recastId) && t.expires > now)
            expires = t.expires + duration; // charges stack on the time still remaining
        IAbility* a            = ctx.core->GetResourceManager()->GetAbilityById(ability + 0x200);
        const std::string name = a != nullptr && a->Name[0] != nullptr && a->Name[0][0] != '\0' ? a->Name[0] : "Ability " + std::to_string(ability);
        t.member   = member;
        t.ability  = name;
        t.tooltip  = std::string(member) + " used " + name + ".\n" +
                    (fromPacket ? "Recast reported by the server (includes their merits and gear)." : "Base recast from the server data; their merits may shorten it.");
        t.recastId = recastId;
        t.job      = job;
        t.expires  = expires;
        t.total    = expires - now;
    }

    void RecastModule::Frame(Context& ctx)
    {
        auto* mm = ctx.core->GetMemoryManager();
        auto* rc = mm != nullptr ? mm->GetRecast() : nullptr;
        if (rc == nullptr)
            return;
        const double now = Now();
        for (auto& [key, t] : timers_)
            t.seen = false;
        auto update = [&](uint32_t key, std::string name, double secs, uint32_t icon, bool spell, double total, std::string tooltip) {
            auto& t = timers_[key];
            if (t.done != 0 || t.created == 0 || secs > t.left + 0.5) // new, or used again: a fresh bar
                t.created = now, t.total = secs;
            if (total > 0)
                t.total = total;
            t.total   = std::max(t.total, secs);
            t.name    = std::move(name);
            t.tooltip = std::move(tooltip);
            t.left    = secs;
            t.icon    = icon;
            t.spell   = spell;
            t.seen    = true;
            t.done    = 0;
        };
        if (abilities_)
        {
            auto* player       = mm->GetPlayer();
            const uint32_t job = player != nullptr ? player->GetMainJob() : 0;
            for (uint32_t x = 0; x < 32; ++x)
            {
                const uint32_t id = rc->GetAbilityTimerId(x), timer = rc->GetAbilityTimer(x);
                if ((id == 0 && x != 0) || id == 92 || timer == 0) // 92: second rune enchantment entry
                    continue;
                const double secs = timer / 60.0;
                if (x != 0 && IsCharge(id))
                {
                    // Charges: base recast (+ merit / gear modifier) split into equal charges.
                    const double baseRecast = (id == 102 ? 90.0 : id == 195 ? 120.0 : 240.0) + ChargeModifier(x);
                    double count            = id == 102 ? 3.0 : 2.0;
                    if (id == 231)
                    {
                        const uint32_t lvl = player == nullptr ? 0 : player->GetMainJob() == 20 ? player->GetMainJobLevel() : player->GetSubJobLevel();
                        count              = lvl == 0 ? 2.0 : std::floor((static_cast<double>(lvl) - 10.0) / 20.0) + 1.0;
                    }
                    const double charge = baseRecast / std::max(1.0, count);
                    const int charges   = static_cast<int>(std::floor((baseRecast - secs) / charge));
                    const double next   = std::fmod(secs, charge);
                    const char* label   = id == 102 ? "Ready" : id == 195 ? "Quick Draw" : "Stratagems";
                    char tip[160]{};
                    ::sprintf_s(tip, "Current Charges: %d/%d\nNext Charge: %s\nFull Charges: %s", std::max(0, charges), static_cast<int>(count),
                        ChargeText(next).c_str(), ChargeText(secs).c_str());
                    update(0x10000u | id, std::string(label) + "[" + std::to_string(std::max(0, charges)) + "]", next, id | (job << 16), false, charge, tip);
                    continue;
                }
                update(0x10000u | (x == 0 ? 0xFFFFu : id), AbilityName(ctx, x, id), secs, (x == 0 ? 0u : id) | (job << 16), false, 0, std::string());
            }
        }
        // Spell recasts: scan all 1024 slots 10x a second, then only the active ones each frame.
        if (spells_ && now - spellScanAt_ >= 0.1)
        {
            spellScanAt_ = now;
            activeSpells_.clear();
            for (uint32_t x = 0; x < 1024; ++x)
                if (rc->GetSpellTimer(x) > 0)
                    activeSpells_.push_back(static_cast<uint16_t>(x));
        }
        if (spells_)
            for (const uint32_t x : activeSpells_)
                if (const uint32_t timer = rc->GetSpellTimer(x); timer > 0)
                {
                    ISpell* s = ctx.core->GetResourceManager()->GetSpellById(x);
                    update(x, s != nullptr && s->Name[0] != nullptr && s->Name[0][0] != '\0' ? s->Name[0] : "Unknown Spell [" + std::to_string(x) + "]",
                        timer / 60.0, x, true, 0, std::string());
                }
        // Finished: flash for the completion time (when animated), then drop.
        for (auto it = timers_.begin(); it != timers_.end();)
        {
            auto& t = it->second;
            if (!t.seen)
            {
                if (t.done == 0)
                    t.done = now;
                t.left = t.done - now; // negative while completing (sorts first, like tTimers)
                if (!animate_ || now - t.done > completion_)
                {
                    it = timers_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        const double keep = animate_ ? completion_ : 0.0;
        for (auto it = partyTimers_.begin(); it != partyTimers_.end();)
            it = (it->second.expires - now < -keep) ? partyTimers_.erase(it) : std::next(it);
    }

    void RecastModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        const double now = Now();
        std::vector<Row> rows;
        if (recast_.enabled)
        {
            rows.reserve(timers_.size());
            for (const auto& [key, t] : timers_)
            {
                const uint32_t id = t.icon & 0xFFFF, job = t.icon >> 16;
                rows.push_back({&t.name, &t.tooltip, Icon(ctx, t.spell ? "spells" : "abilities", id, t.spell ? 0 : job), t.left, t.total, t.done != 0});
            }
            DrawGroup(ctx, gui, "##fpui_recast", rows, recast_, 0);
        }
        if (party_ && partyGroup_.enabled)
        {
            rows.clear();
            std::vector<std::string> labels;
            labels.reserve(partyTimers_.size());
            for (const auto& [key, t] : partyTimers_)
                labels.push_back(memberName_ ? t.member + ": " + t.ability : t.ability);
            size_t i = 0;
            for (const auto& [key, t] : partyTimers_)
            {
                const double left = t.expires - now;
                rows.push_back({&labels[i++], &t.tooltip, t.recastId < 0x10000 ? Icon(ctx, "abilities", t.recastId, t.job) : nullptr, left, t.total, left <= 0});
            }
            DrawGroup(ctx, gui, "##fpui_partytimers", rows, partyGroup_, 1);
        }
    }

    // One tTimers stack: soonest first, the first row at the anchor (growing up when bottom-justified).
    void RecastModule::DrawGroup(Context& ctx, IGuiManager* gui, const char* id, std::vector<Row>& rows, Group& grp, int index)
    {
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.left == b.left ? *a.name < *b.name : a.left < b.left; });
        if (static_cast<int>(rows.size()) > grp.maxRows)
            rows.resize(static_cast<size_t>(grp.maxRows));
        if (rows.empty() && locked_)
            return;
        const float s = grp.scale, w = kBarW * s, h = kBarH * s, step = kStep * s;
        gui->SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        gui->SetNextWindowSize(ImVec2(ctx.layout.screenW, ctx.layout.screenH), ImGuiCond_Always);
        gui->SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | (locked_ ? ImGuiWindowFlags_NoInputs : 0) | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove;
        if (gui->Begin(id, nullptr, f))
        {
            IDirect3DTexture8* outline = ctx.textures != nullptr ? ctx.textures->File(ctx.resourceDir + "ttimers\\outline_farplane9.png") : nullptr;
            IDirect3DTexture8* bar     = ctx.textures != nullptr ? ctx.textures->File(ctx.resourceDir + "ttimers\\bar_farplane9.png") : nullptr;
            const ImVec2 mouse         = gui->GetIO().MousePos;
            const bool flashOn         = std::fmod(Now(), 0.4) < 0.2; // completion flash: 0.2 s on / off
            gui->PushFont(font_, kFont * s * 1.25f);                  // gdi point height -> ImGui pixel size
            const float textH      = gui->GetTextLineHeight();
            const std::string* tip = nullptr;
            float y                = grp.bottom ? grp.y - step : grp.y;
            for (const Row& r : rows)
            {
                const float x = grp.x;
                if (outline != nullptr && look_ == 0)
                    ui::ImageAt(gui, outline, ImVec2(x, y), ImVec2(w, h), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                const Rgba col = BarColor(r.done ? 0.0 : r.left, grp.reverse);
                float frac     = 0;
                if (r.done)
                    frac = flashOn ? 1.0f : 0.0f;
                else
                {
                    const float pct = static_cast<float>(std::clamp(r.left / std::max(0.01, r.total), 0.0, 1.0));
                    frac            = grp.countDown ? pct : 1.0f - pct; // count up: fills as the recast runs out
                }
                if (look_ == 1) // Bar Styles (shape / fill from that module; trough included)
                    mod::DrawBar(ctx, gui, ImVec2(x, y), ImVec2(w, h), frac, col, 1.0f, false);
                else if (frac > 0)
                {
                    if (bar != nullptr)
                        ui::ImageAt(gui, bar, ImVec2(x, y), ImVec2(w * frac, h), ImVec2(0, 0), ImVec2(frac, 1), col);
                    else if (ctx.white != nullptr)
                        ui::ImageAt(gui, ctx.white, ImVec2(x + 1, y + 1), ImVec2((w - 2) * frac, h - 2), ImVec2(0, 0), ImVec2(1, 1), col);
                }
                if (r.icon != nullptr)
                    ui::ImageAt(gui, r.icon, ImVec2(x, y), ImVec2(kIcon * s, kIcon * s), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
                const float ty = y + (h - textH) * 0.5f;
                auto outlined  = [&](float tx, const std::string& text) {
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            if (dx != 0 || dy != 0)
                            {
                                gui->SetCursorScreenPos(ImVec2(tx + dx, ty + dy));
                                gui->TextColored(ToVec4(kOutline), "%s", text.c_str());
                            }
                    gui->SetCursorScreenPos(ImVec2(tx, ty));
                    gui->TextColored(ToVec4(kText), "%s", text.c_str());
                };
                outlined(x + kNameX * s, *r.name);
                if (!r.done && r.left > 0)
                {
                    const std::string t = TimeText(r.left, tenths_);
                    outlined(x + w - kTimerX * s - gui->CalcTextSize(t.c_str()).x, t);
                }
                if (tooltips_ && r.tooltip != nullptr && !r.tooltip->empty() && mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h)
                    tip = r.tooltip;
                y += grp.bottom ? -step : step;
            }
            gui->PopFont();
            if (tip != nullptr)
                gui->SetTooltip("%s", tip->c_str());
            // Unlocked: tTimers-style drag handle just left of the anchor.
            if (!locked_)
            {
                const float hs = kHandle * s * 0.5f;
                const ImVec2 hp(grp.x - hs - 4, grp.bottom ? grp.y - hs : grp.y);
                if (ctx.white != nullptr)
                    ui::ImageAt(gui, ctx.white, hp, ImVec2(hs, hs), ImVec2(0, 0), ImVec2(1, 1), kHandleFill);
                gui->SetCursorScreenPos(hp);
                gui->PushID(index);
                gui->InvisibleButton("##drag", ImVec2(hs, hs));
                if (gui->IsItemActive())
                {
                    const ImVec2 d = gui->GetIO().MouseDelta;
                    grp.x += d.x, grp.y += d.y;
                    ctx.MarkDirty();
                }
                if (gui->IsItemHovered())
                    gui->SetTooltip(index == 0 ? "Drag: your recasts" : "Drag: party abilities");
                gui->PopID();
            }
        }
        gui->Dummy(ImVec2(0, 0)); // ImGui 1.92: an item must follow any SetCursorScreenPos before End
        gui->End();
    }

    void RecastModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "timers";
        show_          = cfg->GetBool(alias, S, "show", show_);
        abilities_     = cfg->GetBool(alias, S, "abilities", abilities_);
        spells_        = cfg->GetBool(alias, S, "spells", spells_);
        party_         = cfg->GetBool(alias, S, "party", party_);
        partySelf_     = cfg->GetBool(alias, S, "partySelf", partySelf_);
        partyAlliance_ = cfg->GetBool(alias, S, "partyAlliance", partyAlliance_);
        memberName_    = cfg->GetBool(alias, S, "memberName", memberName_);
        tenths_        = cfg->GetBool(alias, S, "tenths", tenths_);
        animate_       = cfg->GetBool(alias, S, "animate", animate_);
        tooltips_      = cfg->GetBool(alias, S, "tooltips", tooltips_);
        locked_        = cfg->GetBool(alias, S, "locked", locked_);
        look_          = std::clamp(cfg->GetInt32(alias, S, "look", look_), 0, 1);
        completion_    = std::clamp(cfg->GetFloat(alias, S, "completion", completion_), 0.0f, 10.0f);
        auto group = [&](const char* p, Group& g) {
            const std::string k(p);
            g.enabled   = cfg->GetBool(alias, S, (k + "Enabled").c_str(), g.enabled);
            g.x         = cfg->GetFloat(alias, S, (k + "X").c_str(), g.x);
            g.y         = cfg->GetFloat(alias, S, (k + "Y").c_str(), g.y);
            g.scale     = std::clamp(cfg->GetFloat(alias, S, (k + "Scale").c_str(), g.scale), 0.5f, 3.0f);
            g.maxRows   = std::clamp(cfg->GetInt32(alias, S, (k + "Rows").c_str(), g.maxRows), 1, 30);
            g.bottom    = cfg->GetBool(alias, S, (k + "Bottom").c_str(), g.bottom);
            g.countDown = cfg->GetBool(alias, S, (k + "CountDown").c_str(), g.countDown);
            g.reverse   = cfg->GetBool(alias, S, (k + "Reverse").c_str(), g.reverse);
        };
        group("recast", recast_);
        group("party", partyGroup_);
    }
    void RecastModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "timers";
        ui::SetB(cfg, alias, S, "show", show_);
        ui::SetB(cfg, alias, S, "abilities", abilities_);
        ui::SetB(cfg, alias, S, "spells", spells_);
        ui::SetB(cfg, alias, S, "party", party_);
        ui::SetB(cfg, alias, S, "partySelf", partySelf_);
        ui::SetB(cfg, alias, S, "partyAlliance", partyAlliance_);
        ui::SetB(cfg, alias, S, "memberName", memberName_);
        ui::SetB(cfg, alias, S, "tenths", tenths_);
        ui::SetB(cfg, alias, S, "animate", animate_);
        ui::SetB(cfg, alias, S, "tooltips", tooltips_);
        ui::SetB(cfg, alias, S, "locked", locked_);
        ui::SetI(cfg, alias, S, "look", look_);
        ui::SetF(cfg, alias, S, "completion", completion_);
        auto group = [&](const char* p, const Group& g) {
            const std::string k(p);
            ui::SetB(cfg, alias, S, (k + "Enabled").c_str(), g.enabled);
            ui::SetF(cfg, alias, S, (k + "X").c_str(), g.x);
            ui::SetF(cfg, alias, S, (k + "Y").c_str(), g.y);
            ui::SetF(cfg, alias, S, (k + "Scale").c_str(), g.scale);
            ui::SetI(cfg, alias, S, (k + "Rows").c_str(), g.maxRows);
            ui::SetB(cfg, alias, S, (k + "Bottom").c_str(), g.bottom);
            ui::SetB(cfg, alias, S, (k + "CountDown").c_str(), g.countDown);
            ui::SetB(cfg, alias, S, (k + "Reverse").c_str(), g.reverse);
        };
        group("recast", recast_);
        group("party", partyGroup_);
    }
    auto RecastModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "show" || sub == "hide" || sub == "toggle")
        {
            show_ = sub == "toggle" ? !show_ : sub == "show";
            ctx.MarkDirty();
            return true;
        }
        if (sub == "lock" || sub == "unlock")
        {
            locked_ = sub == "lock";
            ctx.MarkDirty();
            return true;
        }
        if (sub == "clear")
        {
            partyTimers_.clear();
            return true;
        }
        ctx.Print("/fpui recast [show|hide] | unlock (drag the handles) | lock | clear (party timers)");
        return true;
    }
    void RecastModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        bool ch = g.Toggle("Enabled", "tTimers-style recast bars (Farplane IX skin).", show_);
        ch |= g.Toggle("Locked", "Turn off to show a drag handle next to each group's anchor.", locked_);
        static const char* const kLooks[] = {"Farplane IX (tTimers)", "Bar Styles"};
        ch |= g.Choice("Bar look", "Farplane IX: the tTimers skin. Bar Styles: the shape and fill chosen in the Bar Styles module (its \"Timers and cast bar\" switch must be on).", look_, kLooks, 2);
        ch |= g.Toggle("Show tenths", "A decimal place under one minute.", tenths_);
        ch |= g.Toggle("Animate completion", "A finished timer flashes its full bar before leaving.", animate_);
        ch |= g.Slider("Completion time", "Seconds a finished timer flashes.", completion_, 0.0f, 10.0f, "%.0f");
        ch |= g.Toggle("Tooltips", "Hover a bar for details (charges, who used it).", tooltips_);
        auto group = [&](Group& grp, float defX, float defY) {
            ch |= g.Toggle("Show group", "Show this group.", grp.enabled);
            ch |= g.Slider("Scale", "Size multiplier (tTimers Scale).", grp.scale, 0.5f, 3.0f, "%.2f");
            ch |= g.Slider("Maximum timers", "Most bars shown at once.", grp.maxRows, 1, 30);
            ch |= g.Toggle("Bottom justified", "Grow upward from the anchor.", grp.bottom);
            ch |= g.Toggle("Count down", "Bars drain as time runs out (off: they fill up as the recast finishes).", grp.countDown);
            ch |= g.Toggle("Reverse colors", "Off: green when nearly ready, red for long waits.", grp.reverse);
            ch |= g.Slider("Horizontal position", "Anchor, pixels from the left.", grp.x, 0.0f, ctx.layout.screenW, "%.0f");
            ch |= g.Slider("Vertical position", "Anchor, pixels from the top.", grp.y, 0.0f, ctx.layout.screenH, "%.0f");
            if (g.Button("Position", "Back to the tTimers default spot.", "Reset"))
                grp.x = defX, grp.y = defY, ch = true;
        };
        g.Section("Recasts");
        ch |= g.Toggle("Abilities", "Your job ability recasts (one-hours, charges included).", abilities_);
        ch |= g.Toggle("Spells", "Your spell recasts.", spells_);
        gui->PushID("recast");
        group(recast_, 592.0f, 516.0f);
        gui->PopID();
        g.Section("Party abilities");
        ch |= g.Toggle("Party abilities", "Party members' job ability recasts.", party_);
        ch |= g.Toggle("Show member name", "\"Name: Ability\" labels.", memberName_);
        ch |= g.Toggle("Include yourself", "Also list your own job abilities here.", partySelf_);
        ch |= g.Toggle("Include alliance", "Also track alliance members.", partyAlliance_);
        gui->PushID("party");
        group(partyGroup_, 1557.0f, 517.0f);
        gui->PopID();
        g.Info("Source", "Recast reported by the server (their merits and gear included); Blood Pacts use the base recast.");
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
