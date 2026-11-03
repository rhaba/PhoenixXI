/**
 * Farplane UI - built-in modules.
 */

#ifndef FARPLANEUI_MODULES_HPP_INCLUDED
#define FARPLANEUI_MODULES_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "chatparse.hpp"

#include <set>
#include "mask.hpp"
#include "mobdb.hpp"
#include "module.hpp"
#include "settings.hpp"

#include <array>
#include <ctime>
#include <deque>
#include <unordered_map>

namespace farplaneui
{
    // The bottom UI mask (port of farplaneui.lua). Also owns the global [settings] (theme etc.).
    // The host forwards the Direct3D draw hooks to it directly (it must draw under the game UI).
    class MaskModule final : public IModule
    {
    public:
        Settings settings{};
        Mask mask{};
        int theme{kDefaultTheme};
        bool hidden{false};

        auto Name(void) const -> const char* override { return "mask"; }
        auto Title(void) const -> const char* override { return "Background & mask"; }
        auto SettingsSections(void) const -> std::vector<const char*> override { return {"Background", "General", "Size"}; }
        void Direct3DInit(Context& ctx) override;
        void Release(void) override { mask.Release(); }
        void Render(Context& ctx, IGuiManager* gui) override; // setup label
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        // Band edges for the layout without drawing.
        void Band(float& top, float& bottom) const;
        void SetTheme(int index);
        void Report(Context& ctx) const;
    };

    class PartyModule final : public IModule
    {
        bool show_{true}, docked_{true}, locked_{false};
        Rect user_{1500, 700, 300, 280};
        bool buffs_{true}, footer_{true};
        struct RowPos
        {
            uint32_t slot, serverId;
            float y, h;
        };
        std::vector<RowPos> rowsY_; // member rows this frame (buff icons line up with them)
        float panelX_{0};
        double testUntil_{0}; // /fpui party test: sample party until this time
        void RenderBuffs(Context& ctx, IGuiManager* gui);
        void RenderEmber(Context& ctx, IGuiManager* gui);
        void RenderFxLane(Context& ctx, IGuiManager* gui, bool demo);
        std::unordered_map<uint16_t, double> fxMax_; // your effects: longest time left seen (= full duration)
        int popMember_{-1}, popHoverMember_{-1};     // +N popup
        double popHoverSince_{0}, popLast_{0};
        ImVec2 popA_{}, popB_{};
        auto Buffs(Context& ctx, uint32_t slot, uint32_t serverId) -> std::vector<uint16_t>;

    public:
        auto Name(void) const -> const char* override { return "party"; }
        auto Title(void) const -> const char* override { return "Party"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        auto Shown(void) const -> bool { return show_; }
        void Toggle(Context& ctx) { show_ = !show_; ctx.MarkDirty(); }
    };

    class ChatModule final : public IModule
    {
    public:
        struct Line
        {
            uint64_t seq{0};
            std::time_t time{0};
            int32_t mode{0};
            chat::Cat cat{chat::Cat::System};
            std::vector<chat::Segment> segs;
            std::string plain;
            mutable float height{-1.0f}, heightWrap{0.0f}; // measured on draw (virtualised log)
        };
        struct Tab
        {
            std::string name;
            uint32_t mask{0}; // bit per chat::Cat
        };

    private:
        std::deque<Line> lines_;
        std::deque<std::pair<std::string, ULONGLONG>> npcRecent_; // NPC dialogue lines seen in the last 5 s (dedupe)
        uint64_t seq_{0};
        std::vector<Tab> tabs_;
        int activeTab_{0};
        std::array<Rgba, chat::kCatCount> colors_{};
        ImFont* font_{nullptr};
        std::string fontStatus_{"default font"};

        // settings
        bool show_{true}, docked_{true}, locked_{false};
        bool hideNative_{true}; // default ON (user: hide replaced native UI on load)
        bool timestamps_{true};
        std::string tsFormat_{"[%H:%M]"};
        bool autoScroll_{true};
        bool scrollLock_{false};
        float opacity_{0.0f}; // window bg over the mask (0 = the mask shows through)
        float fontSize_{16.0f};
        float lineGap_{2.0f}; // pixels between chat lines
        std::string fontFile_{"Play-Regular.ttf"};
        int history_{2000};
        bool artFrame_{true};
        Rect user_{20, 700, 700, 260};
        bool scrollToBottom_{false};

        // FarplaneUI chat input line (v0.3.3)
        bool inputEnabled_{true};
        bool yieldToMenus_{true}; // hide while a native game menu is open
        int holdMenuX_{-1};       // experiment: pin the command menu x (-1 = off)
        WPARAM swallowKey_{0};    // key that closed our input line, blocked until released
        ULONGLONG enterBlockUntil_{0}; // Enter stays hidden from DirectInput until this tick
        ULONGLONG submitAt_{0};        // when our line last consumed Enter (submit)
        bool enterLog_{false};         // /fpui chat enterlog: trace every Enter path to <Ashita>\logs
        std::string enterLogFile_;
        static void EnterLog(const char* fmt, ...);
        bool inputOpen_{false};
        bool inputFocus_{false};
        bool inputCursorEnd_{false}; // drop ImGui's select-all on focus so the prefilled "/" survives
        int inputUnfocusedFrames_{0};  // watchdog: close the line if it never gets keyboard focus
        char inputBuf_[256]{};
        std::vector<std::string> inputHistory_;
        int historyPos_{-1};
        std::string openKeys_{"space,slash"};
        bool keyEnter_{false}, keySpace_{true}, keySlash_{true};
        bool nativeSeen_{false};
        void ParseOpenKeys(void);
        IAshitaCore* core_{nullptr};
        void RenderInput(Context& ctx, IGuiManager* gui);
        static auto InputCallback(ImGuiInputTextCallbackData* d) -> int;
        static auto __stdcall KeyboardCallback(WPARAM w, LPARAM l, bool blocked) -> BOOL;
        static auto __stdcall DeviceStateCallback(LPDIRECTINPUTDEVICE8A device, DWORD size, LPVOID data) -> HRESULT;
        static auto __stdcall DeviceDataCallback(LPDIRECTINPUTDEVICE8A device, DWORD objectSize, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags, DWORD extra) -> HRESULT;
        static auto EnterBlocked(void) -> bool;
        // Ctrl+R (reply): opens our line with "/t <last tell sender> " instead of the native bar.
        std::string lastTell_;
        static auto ReplyKeyOurs(void) -> bool;

        void DefaultTabs(void);
        void DefaultColors(void);
        auto TabsToString(void) const -> std::string;
        void TabsFromString(const std::string& s);
        auto Visible(const Line& l, const Tab& t) const -> bool;
        void DrawLine(IGuiManager* gui, const Context& ctx, const Line& l, float wrapWidth);
        auto CopyTab(int maxLines) const -> std::string;

    public:
        auto Name(void) const -> const char* override { return "chat"; }
        auto Title(void) const -> const char* override { return "Chat"; }
        auto SettingsSections(void) const -> std::vector<const char*> override { return {"General", "Text", "Tabs", "Colors"}; }
        void Initialize(Context& ctx) override;
        void Direct3DInit(Context& ctx) override;
        void Release(void) override;
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        auto IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool override;
        auto LineCount(void) const -> size_t { return lines_.size(); }
        auto HideNativeFlag(void) -> bool* { return &hideNative_; }
    };

    // Skeleton: frame slot, zone id, player position/heading; optional calibrated PNG map.
    class MinimapModule final : public IModule
    {
        struct MapEntry
        {
            uint32_t zone{0}, floorId{0}, floorIndex{0}, flags{0}, datOffset{0};
            int scale{0}, keyOffset{0}, offsetX{0}, offsetY{0};
        };
        bool show_{true}, docked_{true}, locked_{false};
        float zoom_{1.0f};
        Rect user_{760, 760, 220, 220};
        bool scanned_{false}, hasEntry_{false};
        uintptr_t table_{0}, floorFn_{0}, floorThis_{0};
        uint32_t curZone_{0xFFFFFFFFu};
        int32_t curFloor_{-1};
        ULONGLONG lastFloorCheck_{0};
        MapEntry entry_{};
        IDirect3DTexture8* tex_{nullptr};    // current map (owned only when == datTex_)
        IDirect3DTexture8* datTex_{nullptr}; // texture decoded from a DAT (we release it)
        UINT texW_{0}, texH_{0};
        std::string source_, status_;
        IDirect3DTexture8* arrowTex_{nullptr}; // generated atlas of 64 pre-rotated player arrows
        bool enemies_{true};                   // draw nearby monsters as dots
        void Scan(Context& ctx);
        void BuildArrow(Context& ctx);
        auto ReadEntry(uint32_t i, MapEntry& e) const -> bool;
        auto CurrentFloor(float x, float y, float z) const -> int32_t;
        void SelectMap(Context& ctx, uint32_t zone, int32_t floor);
        auto LoadDat(Context& ctx, uint32_t fileId) -> bool;
        void FreeMap(void);

    public:
        void Release(void) override;
        auto Name(void) const -> const char* override { return "minimap"; }
        auto Title(void) const -> const char* override { return "Minimap"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    class InventoryModule final : public IModule
    {
        bool show_{true}, docked_{true}, locked_{false};
        uint32_t bags_{0x1u | 0x20u | 0x40u | 0x80u | 0x100u}; // inventory, satchel, sack, case, wardrobe
        Rect user_{1100, 760, 200, 220};

    public:
        auto Name(void) const -> const char* override { return "inventory"; }
        auto Title(void) const -> const char* override { return "Inventory"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    class EquipModule final : public IModule
    {
        bool show_{true}, docked_{true}, locked_{false};
        float cell_{40.0f};
        Rect user_{1700, 760, 180, 180};

    public:
        auto Name(void) const -> const char* override { return "equip"; }
        auto Title(void) const -> const char* override { return "Equipment"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        auto Cell(void) const -> float { return cell_; }
    };
    // Hides native FFXI UI that FarplaneUI replaces; restores it on unload / toggle off.
    // Party/alliance frames + target frame: the primitive-visibility method of atom0s's official
    // Ashita addon "hideparty" (Ashita-v4beta/addons/hideparty). Chat log: see ChatModule.
    class NativeUiModule final : public IModule
    {
        bool hideParty_{true}, hideTarget_{false};
        bool scanned_{false}, found_{false};
        uintptr_t party0_{0}, party1_{0}, party2_{0}, target_{0};
        bool hideCompass_{true}, hideClock_{true}, hideBuffs_{true}, hideCastBar_{true};
        bool clockHidden_{false};
        uintptr_t compass_{0}, buffs_{0}, castbar_{0}, targetWin_{0}; // menu slots found by name
        uintptr_t compassByte_{0}; // code byte gating the native compass draw (1 = drawn, 0 = not); see mod_native.cpp
        uintptr_t manager_{0};     // game UI manager: live window list (for /fpui native windows | hidewin)
        std::vector<std::string> hideWins_; // extra live windows hidden by name ([native] hideWindows)
        bool fishing_{false};  // player in a fishing status (shown by /fpui native)
        bool wasEnabled_{true};
        bool enabled_{true};   // master switch: /fpui native all on|off (off = the game UI is left alone)
        bool noTarget_{false}; // nothing targeted (shown by /fpui native)
        bool targetIsObject_{false}; // a door / counter etc. is targeted: the game's own target frame shows
        uint32_t status_{0};
        void ApplyLive(bool restoreAll);
        void ListWindows(Context& ctx);
        IAshitaCore* core_{nullptr};
        void SetClock(bool hidden);
        uintptr_t chatWin1_{0}, chatWin2_{0}; // addresses of the code operands holding the window slots
        bool chatFound_{false};
        bool* hideChat_{nullptr}; // ChatModule's flag (shown in this tab too)
        void Scan(Context& ctx);
        void Apply(bool restoreAll);

    public:
        explicit NativeUiModule(bool* hideChat) : hideChat_{hideChat} {}
        auto Name(void) const -> const char* override { return "native"; }
        auto Title(void) const -> const char* override { return "Native UI"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Release(void) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // World-space ground ring + facing arrow under the current target, and an HP bar near it.
    // Top-centre target bar: name (claim colour), level range, distance, HP, and mobdb data -
    // aggro/link/detection, damage weaknesses/resistances and status immunities.
    class TargetBarModule final : public IModule
    {
        bool show_{true}, docked_{false}, locked_{false};
        bool showResists_{true}, showImmunities_{true};
        Rect user_{-1, 18, 440, 96}; // x < 0: centre on first use
        MobDb& db_{MobDb::Shared()};
        bool slim_{false}; // slim bar: name, HP and distance only (level / detection / resists move to the ring)
        int lines_{1}; // text lines the last target needed; extra lines grow the panel upward
        bool infoOpen_{false}; // mob info window ("i" on the bar)
        ImVec2 infoPos_{-1, -1};
        int th_{0};            // treasure hunter level for drop rates
        void RenderInfo(Context& ctx, IGuiManager* gui);

    public:
        auto Name(void) const -> const char* override { return "targetbar"; }
        auto Title(void) const -> const char* override { return "Target Bar"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;

    private:
        void RenderTarget(Context& ctx, IGuiManager* gui, uint32_t idx);
    };

    // Points strip (port of the points addon by Shinzaku, based on Byrthnoth's Pointwatch):
    // job/level, XP or LP, merits, XP/hr, chain + timer, CP/JP at 99.
    class PointsModule final : public IModule
    {
    public:
        struct Kill
        {
            double gap; // seconds since the previous kill of the same kind
            uint32_t value;
        };

    private:
        bool show_{true}, docked_{true}, locked_{false};
        Rect user_{900, 600, 520, 46};
        std::vector<Kill> xpKills_, cpKills_;
        double lastXpKill_{0}, lastCpKill_{0};
        double xpChainEnd_{0}, cpChainEnd_{0};
        uint32_t xpChain_{0}, cpChain_{0};

    public:
        auto Name(void) const -> const char* override { return "points"; }
        auto Title(void) const -> const char* override { return "Points"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Strip centre. Resting: next HP/MP tick, MP (or HP) to full, last measured gains.
    // Engaged: next auto-attack swing, rounds since engaging, TP and seconds to 1000 TP.
    class TicksModule final : public IModule
    {
        bool show_{true}, docked_{true}, locked_{false};
        Rect user_{900, 560, 260, 46};
        bool resting_{false};
        double startedAt_{0}, nextTick_{0};
        uint32_t ticks_{0}, lastHp_{0}, lastMp_{0}, lastHpGain_{0}, lastMpGain_{0};
        // Auto-attack, measured from our own melee rounds (action packet category 1).
        bool engaged_{false};
        uint32_t rounds_{0}, roundTp_{0};
        double lastRound_{0};
        std::deque<double> swingGaps_;
        std::deque<uint32_t> tpGains_;
        void RenderResting(Context& ctx, IGuiManager* gui, float width, float& frac, std::string& below);
        void RenderAttack(Context& ctx, IGuiManager* gui, float width, float& frac);

    public:
        auto Name(void) const -> const char* override { return "ticks"; }
        auto Title(void) const -> const char* override { return "Ticks / Auto-attack"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Event notifications left of the character: XP/LP/CP, items, gil, skill ups, key items,
    // treasure pool, party invitations and trade requests (with countdowns).
    class NotifyModule final : public IModule
    {
    public:
        struct Note
        {
            std::string text;
            Rgba color{1, 1, 1, 1};
            uint16_t itemId{0}; // icon (0 = none, 0xFFFF = gil)
            bool countdown{false};
            double born{0}, expires{0};
        };

    private:
        static constexpr int kTypes = 8;
        enum { kXpType, kItemType, kGilType, kSkillType, kKeyType, kTreasureType, kInviteType, kTradeType };
        static constexpr const char* kTypeKeys[kTypes]   = {"xp", "items", "gil", "skills", "keyitems", "treasure", "invites", "trades"};
        static constexpr const char* kTypeTitles[kTypes] = {"Experience", "Items", "Gil", "Skill ups", "Key items", "Treasure pool",
            "Party invitations", "Trade requests"};
        static constexpr const char* kTypeDescs[kTypes] = {"XP, limit and capacity points gained.", "Items added to any bag.",
            "Gil gained or spent.", "Combat, magic and crafting skill ups.", "Newly obtained key items.", "New items in the treasure pool.",
            "Party and alliance invitations, with a 90s countdown.", "Incoming trade requests, with a 30s countdown."};
        bool show_{true};
        bool on_[kTypes]{true, true, true, true, true, true, true, true};
        float lifetime_{6.0f}, offsetX_{0.0f}, offsetY_{0.0f};
        int maxRows_{8};
        std::deque<Note> notes_;
        std::unordered_map<uint16_t, uint32_t> counts_;
        uint32_t lastGil_{0};
        bool haveSnapshot_{false};
        double lastScan_{0}, quietUntil_{0};
        void Push(Note n);

    public:
        auto Name(void) const -> const char* override { return "notify"; }
        auto Title(void) const -> const char* override { return "Notifications"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        auto IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Timers in the tTimers "classic" look (Farplane IX skin): your ability / spell recasts and party
    // members' job ability recasts (from action packets; port of the tTimers party fork), two stacks.
    class RecastModule final : public IModule
    {
        struct Timer
        {
            std::string name, tooltip;
            double left{0}, total{0}, created{0}, done{0}; // done: when it finished (0 = running)
            uint32_t icon{0};                              // id | main job << 16
            bool spell{false}, seen{false};
        };
        struct PartyTimer
        {
            std::string member, ability, tooltip;
            uint32_t recastId{0}, job{0};
            double expires{0}, total{0};
        };
        struct Group
        {
            float x, y, scale;
            int maxRows;
            bool enabled{true}, bottom{true}, countDown{false}, reverse{false};
        };
        struct Row
        {
            const std::string *name, *tooltip;
            IDirect3DTexture8* icon;
            double left, total;
            bool done;
        };
        bool show_{true}, abilities_{true}, spells_{true}, tenths_{true}, animate_{true}, tooltips_{true}, locked_{true};
        int look_{0}; // bar look: 0 Farplane IX (tTimers skin), 1 the Bar Styles setting
        bool party_{true}, partySelf_{false}, partyAlliance_{false}, memberName_{true};
        float completion_{3.0f};
        Group recast_{592.0f, 516.0f, 1.3f, 10}, partyGroup_{1557.0f, 517.0f, 1.3f, 12}; // tTimers settings
        std::unordered_map<uint32_t, Timer> timers_;           // key: spell id, 0x10000 | ability timer id
        std::unordered_map<uint64_t, PartyTimer> partyTimers_; // key: member server id << 32 | recast group
        std::vector<uint16_t> activeSpells_;                   // spell ids with a recast, rescanned 10x a second
        double spellScanAt_{0};
        std::unordered_map<uint32_t, std::string> abilityNames_;
        uintptr_t recastBase_{0};
        bool recastSigTried_{false};
        ImFont* font_{nullptr}; // Play, like the skin
        auto AbilityName(Context& ctx, uint32_t slot, uint32_t timerId) -> std::string;
        auto Icon(Context& ctx, const char* dir, uint32_t id, uint32_t job) -> IDirect3DTexture8*;
        auto ChargeModifier(uint32_t slot) -> int;
        void DrawGroup(Context& ctx, IGuiManager* gui, const char* id, std::vector<Row>& rows, Group& grp, int index);

    public:
        auto Name(void) const -> const char* override { return "recast"; }
        auto Title(void) const -> const char* override { return "Timers"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Direct3DInit(Context& ctx) override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Cast bar below the character: the game's own cast progress, spell / item name from action packets.
    class CastBarModule final : public IModule
    {
        bool show_{true}, items_{true}, showPercent_{true};
        float width_{260.0f}, offsetX_{0.0f}, offsetY_{0.0f};
        std::string name_;
        bool item_{false};
        float lastPct_{0};
        double lastSeen_{0}, interruptedAt_{0};

    public:
        auto Name(void) const -> const char* override { return "castbar"; }
        auto Title(void) const -> const char* override { return "Cast Bar"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Player status effects centred below the cast bar: icons with time left.
    class StatusModule final : public IModule
    {
        bool show_{true};
        float iconSize_{24.0f}, x_{6.0f}, y_{6.0f}; // top-left corner of the grid, like the native icons
        bool locked_{true}; // unlocked: drag the icons to move them
        int perRow_{16};

    public:
        auto Name(void) const -> const char* override { return "status"; }
        auto Title(void) const -> const char* override { return "Status Effects"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // Enemy list (left): monsters engaged with the party, who they target, HP; click to target.
    class EnemyModule final : public IModule
    {
        bool show_{true}, locked_{false}, clickTarget_{true};
        int maxRows_{8};
        Rect user_{-1, 0, 250, 60}; // x < 0: default spot on first use
        std::unordered_map<uint32_t, double> aggro_; // monster server id -> last time it acted on the party
        double testUntil_{0};

    public:
        auto Name(void) const -> const char* override { return "enemies"; }
        auto Title(void) const -> const char* override { return "Enemy List"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    // Ready check over party chat, compatible with XIUI's (see mod_readycheck.cpp for the format).
    class ReadyCheckModule final : public IModule
    {
        struct Member
        {
            std::string name;
            uint8_t job{0};
            int state{0}; // 0 waiting, 1 ready, 2 not ready
        };
        bool enabled_{true}, summary_{false};
        bool active_{false}, open_{false}, mine_{false}, summarySent_{false}, preview_{false};
        std::vector<Member> members_;
        std::string initiator_;
        double startedAt_{0}, endedAt_{0};
        Rect user_{-1, 0, 230, 60};
        void Begin(Context& ctx, const std::string& initiator);
        void Send(Context& ctx, const std::string& text);
        void Answer(Context& ctx, bool ready);

    public:
        auto Name(void) const -> const char* override { return "readycheck"; }
        auto Title(void) const -> const char* override { return "Ready Check"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        auto IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool override;
    };

    // Alliance parties 2 and 3 (right side), each shown only while that party has members.
    class AllianceModule final : public IModule
    {
        bool show_{true}, locked_{false};
        Rect user_[2]{{-1, 0, 250, 60}, {-1, 0, 250, 60}};
        double testUntil_{0};
        void RenderParty(Context& ctx, IGuiManager* gui, int which);

    public:
        auto Name(void) const -> const char* override { return "alliance"; }
        auto Title(void) const -> const char* override { return "Alliance"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // Compass (turns with the camera) and Vana'diel clock, far left on the chat panel's top edge.
    class CompassModule final : public IModule
    {
        bool show_{true}, showCompass_{true}, showClock_{true}, showDate_{false}, showDay_{false};
        float scale_{1.0f}, offsetX_{0.0f}, offsetY_{0.0f};

    public:
        auto Name(void) const -> const char* override { return "compass"; }
        auto Title(void) const -> const char* override { return "Compass / Clock"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // Moves the game's own command menu (and Magic / Abilities lists) to a draggable COMMAND MENU frame.
    class CommandMenuModule final : public IModule
    {
        struct Win
        {
            uintptr_t window;
            int16_t left, top, right, bottom;
        };
        bool enabled_{true}, editing_{false}, scanned_{false};
        Rect user_{-1, 0, 150, 220};
        uintptr_t setPos_{0}, manager_{0};
        std::unordered_map<uintptr_t, uint32_t> orig_; // window -> position the game gave it
        void Scan(Context& ctx);
        auto Windows(void) const -> std::vector<Win>;

    public:
        auto Name(void) const -> const char* override { return "cmdmenu"; }
        auto Title(void) const -> const char* override { return "Command Menu"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void Release(void) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // Bar Style settings page (no on-screen element; see modules/bars.cpp).
    class BarStyleModule final : public IModule
    {
    public:
        auto Name(void) const -> const char* override { return "bars"; }
        auto Title(void) const -> const char* override { return "Bar Style"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
    };

    // DPS meter: party / alliance damage from action packets (message list derived from Parse, BSD).
    class DpsModule final : public IModule
    {
    public:
        static constexpr int kCats = 6; // melee, ranged, weapon skill, magic, abilities, extra
        struct Stat
        {
            std::string name;
            uint64_t total{0};
            uint64_t cats[kCats]{};
            double active{0}, last{0};
        };

    private:
        bool show_{true}, docked_{true}, locked_{false}, hideEmpty_{true}, paused_{false};
        Rect user_{-1, 0, 280, 60};
        std::unordered_map<uint32_t, Stat> stats_;
        double startedAt_{0}, lastHitAt_{0}, testUntil_{0};
        void Add(uint32_t actor, const std::string& name, int cat, uint32_t dmg);
        void Report(Context& ctx, const std::string& channel);

    public:
        auto Name(void) const -> const char* override { return "dps"; }
        auto Title(void) const -> const char* override { return "DPS Meter"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
    };

    class TargetModule final : public IModule
    {
        bool show_{true}, ring_{true}, bar_{true}, useModelSize_{true};
        float ringScale_{1.0f};
        // Ashita (X, Y, Z=height) -> game world. 0 = (x, z, y), confirmed on PhoenixXI 2026-11-02 from
        // /fpui target debug (the only mapping that puts the target a few units from the camera).
        // ini key "axis"; the old auto-detect key "axisMode" is ignored because it could latch a wrong mode.
        int axisMode_{0};
        mutable int lastMode_{0};
        uint32_t prevIdx_{0};
        ImVec2 prevPos_{-1, -1};
        uint32_t skipped_{0};
        // Ember ring: style 0 hairline, 1 HP arc, 2 classic band
        int style_{0};
        float hpShown_{1.0f}, hpGhost_{1.0f};
        double ghostAt_{0}, lastFrameAt_{0};
        uint32_t hpIdx_{0};
        double pulseStart_{-100};      // TP move / cast pulse
        uint32_t pulseActor_{0};
        std::string pulseLabel_;
        bool info_{true}; // ring info overlay
        ImVec2 aTop_{}, aBot_{}, aRight_{}, aCen_{};
        uint32_t anchorIdx_{0};
        double anchorAt_{0};
        float infoBottom_{0};
        void RenderRingInfo(Context& ctx, IGuiManager* gui, uint32_t idx, bool projected, const ImVec2& top, const ImVec2& bot, const ImVec2& right,
            const ImVec2& centre, float dist, float hp, const Rgba& claim, const Rgba& distCol);

    public:
        auto Name(void) const -> const char* override { return "target"; }
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
        auto IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool override;
        auto Title(void) const -> const char* override { return "Target"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        void Debug(Context& ctx) const;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
    };

    // Quest tracker: slide-in panel on the right with tracked quests and each one's next step
    // (data + engine from the questtracker addon; see mod_quests.cpp).
    class QuestModule final : public IModule
    {
        struct Step
        {
            std::string text, zone, grid;
            std::vector<std::pair<int, int>> ev; // (zone, cutscene) that finish it
            int ki{0};                            // holding this key item finishes it
            struct Need
            {
                int id, qty;
                std::string name;
            };
            std::vector<Need> need;
            std::vector<std::pair<int, std::string>> needKi;
        };
        struct Quest
        {
            int id{0}, log{0};
            std::string key, name, startNpc, startZone, startGrid, note;
            std::vector<Step> steps;
        };
        std::map<int, Quest> quests_;
        std::string dataStatus_, char_;
        bool dataTried_{false}, stateDirty_{false};
        std::set<int> active_, completed_, tracked_, seenLog_;
        std::map<int, int> step_; // quest id -> current step (1-based)
        double kiCheckAt_{0}, savedAt_{0}, lastAt_{0};
        bool show_{true}, open_{true}, expanded_{false};
        float width_{300.0f}, slide_{1.0f}, scroll_{0}, contentH_{120.0f};
        Context* ctxSave_{nullptr};
        void LoadData(Context& ctx);
        auto StatePath(Context& ctx) const -> std::string;
        void LoadState(Context& ctx);
        void SaveState(Context& ctx);
        void ImportAddon(Context& ctx);
        auto StepOf(int id) const -> int;
        void SetStep(Context& ctx, int id, int n, bool announce);

    public:
        auto Name(void) const -> const char* override { return "quests"; }
        auto Title(void) const -> const char* override { return "Quests"; }
        void Load(IConfigurationManager* cfg, const char* alias) override;
        void Save(IConfigurationManager* cfg, const char* alias) const override;
        void Frame(Context& ctx) override;
        void Render(Context& ctx, IGuiManager* gui) override;
        void RenderSettings(Context& ctx, IGuiManager* gui) override;
        auto Command(Context& ctx, const std::vector<std::string>& args) -> bool override;
        void IncomingPacket(Context& ctx, uint16_t id, uint32_t size, const uint8_t* data) override;
        void Release(void) override;
    };
} // namespace farplaneui

#endif
