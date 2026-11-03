/**
 * Farplane UI - chat module: a full replacement for the native chat log.
 *
 * Capture: IChatManager text arrives in HandleIncomingText (mode, raw Shift-JIS message).
 * The line is decoded (chatparse), categorized by mode, stored in a ring buffer and drawn in a
 * window docked into the UI mask. Chat INPUT stays native.
 *
 * Hiding the native log: the only documented safe mechanism is the plugin return value of
 * HandleIncomingText - returning true blocks the line from the game's log (this is what
 * FancyChat's blockAll does through the Lua `e.blocked`). Off by default; never blocks NPC
 * dialogue modes 150/151 (their prompts drive the game's dialog box); stops blocking whenever
 * our window is hidden so nothing is ever lost.
 */
#include "common.hpp"

#include <cstdarg>
#include "../ember.hpp"

#include <algorithm>
#include <cstring>

namespace farplaneui
{
    using chat::Cat;
    namespace
    {
        constexpr uint32_t Bit(Cat c) { return 1u << static_cast<uint32_t>(c); }
        constexpr uint32_t kAll = (1u << chat::kCatCount) - 1u;
        const Rgba kAutoTranslate = hex(0x8fd18a);
    } // namespace

    void ChatModule::DefaultTabs(void)
    {
        tabs_ = {
            {"All", kAll},
            {"Chat", Bit(Cat::Say) | Bit(Cat::Shout) | Bit(Cat::Yell) | Bit(Cat::Tell) | Bit(Cat::Party) | Bit(Cat::Linkshell1) |
                         Bit(Cat::Linkshell2) | Bit(Cat::Emote) | Bit(Cat::Unity) | Bit(Cat::Assist)},
            {"Battle", Bit(Cat::Battle) | Bit(Cat::Error)},
            {"System", Bit(Cat::System) | Bit(Cat::Npc) | Bit(Cat::Error)},
        };
    }

    void ChatModule::DefaultColors(void)
    {
        colors_[static_cast<size_t>(Cat::Say)]        = hex(0xf2ead8);
        colors_[static_cast<size_t>(Cat::Shout)]      = hex(0xf0a070);
        colors_[static_cast<size_t>(Cat::Yell)]       = hex(0xe6c070);
        colors_[static_cast<size_t>(Cat::Tell)]       = hex(0xe89be0);
        colors_[static_cast<size_t>(Cat::Party)]      = hex(0x8fd0f5);
        colors_[static_cast<size_t>(Cat::Linkshell1)] = hex(0x9fe39a);
        colors_[static_cast<size_t>(Cat::Linkshell2)] = hex(0x6fc9a0);
        colors_[static_cast<size_t>(Cat::Emote)]      = hex(0xc8c0b0);
        colors_[static_cast<size_t>(Cat::Unity)]      = hex(0xd8c890);
        colors_[static_cast<size_t>(Cat::Assist)]     = hex(0xa0d8b0);
        colors_[static_cast<size_t>(Cat::Battle)]     = art::amber;
        colors_[static_cast<size_t>(Cat::Npc)]        = art::orb_cyan;
        colors_[static_cast<size_t>(Cat::System)]     = hex(0xd8d0c0);
        colors_[static_cast<size_t>(Cat::Error)]      = hex(0xe86060);
    }

    auto ChatModule::TabsToString(void) const -> std::string
    {
        // "Name:cat,cat;Name2:cat"  ("*" = all categories)
        std::string s;
        for (const auto& t : tabs_)
        {
            if (!s.empty())
                s += ';';
            s += t.name + ':';
            if (t.mask == kAll)
            {
                s += '*';
                continue;
            }
            bool first = true;
            for (int i = 0; i < chat::kCatCount; ++i)
            {
                if ((t.mask & (1u << i)) == 0)
                    continue;
                if (!first)
                    s += ',';
                s += chat::kCatNames[i];
                first = false;
            }
        }
        return s;
    }

    void ChatModule::TabsFromString(const std::string& s)
    {
        std::vector<Tab> out;
        size_t start = 0;
        while (start <= s.size())
        {
            size_t end = s.find(';', start);
            if (end == std::string::npos)
                end = s.size();
            const std::string item = s.substr(start, end - start);
            const size_t colon     = item.find(':');
            if (colon != std::string::npos && colon > 0)
            {
                Tab t{item.substr(0, colon), 0};
                std::string cats = item.substr(colon + 1);
                if (cats == "*")
                    t.mask = kAll;
                size_t a = 0;
                while (t.mask != kAll && a < cats.size())
                {
                    size_t b = cats.find(',', a);
                    if (b == std::string::npos)
                        b = cats.size();
                    Cat c{};
                    if (chat::CatFromName(cats.substr(a, b - a), c))
                        t.mask |= Bit(c);
                    a = b + 1;
                }
                out.push_back(t);
            }
            start = end + 1;
        }
        if (out.empty())
            DefaultTabs();
        else
            tabs_ = std::move(out);
        activeTab_ = std::clamp(activeTab_, 0, static_cast<int>(tabs_.size()) - 1);
    }

    namespace { ChatModule* g_chat = nullptr; }

    void ChatModule::Initialize(Context& ctx)
    {
        if (tabs_.empty())
            DefaultTabs();
        core_  = ctx.core;
        g_chat = this;
        auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr;
        auto* kb = im != nullptr ? im->GetKeyboard() : nullptr;
        if (kb != nullptr)
        {
            // The game also reads the keyboard through DirectInput; these remove Enter from what it sees
            // while our line owns it (see EnterBlocked).
            const getdevicedatacallback_f dcb    = &ChatModule::DeviceDataCallback;
            const getdevicestatecallback_f scb   = &ChatModule::DeviceStateCallback;
            const keyboardcallback_f kcb         = &ChatModule::KeyboardCallback;
            kb->AddCallback("farplaneui_chatinput", dcb, scb, kcb);
        }
    }
    void ChatModule::Release(void)
    {
        auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr;
        auto* kb = im != nullptr ? im->GetKeyboard() : nullptr;
        if (kb != nullptr)
            kb->RemoveCallback("farplaneui_chatinput");
        g_chat = nullptr;
    }
    // Window-message keyboard hook: Enter (with the native input closed) opens OUR input line instead
    // of the game's. Returning TRUE blocks the key from the game.
    void ChatModule::ParseOpenKeys(void)
    {
        const std::string k = ui::Lower(openKeys_);
        keyEnter_ = false; // Enter never opens our line: trades and menus confirm with it
        keySpace_ = k.find("space") != std::string::npos;
        keySlash_ = k.find("slash") != std::string::npos;
    }
    // Keyboard hook (Ashita passes vkey + keystroke flags; TRUE blocks the key from the game).
    auto __stdcall ChatModule::KeyboardCallback(WPARAM w, LPARAM l, bool blocked) -> BOOL
    {
        (void)blocked;
        ChatModule* c = g_chat;
        if (c == nullptr || !c->inputEnabled_ || !c->show_ || c->core_ == nullptr)
            return FALSE;
        const bool keyUp = (static_cast<uint32_t>(l) & 0x80000000u) != 0;
        if (w == VK_RETURN)
            EnterLog("WM key %s  open=%d swallow=%d -> %s", keyUp ? "up" : "down", c->inputOpen_ ? 1 : 0, c->swallowKey_ == VK_RETURN ? 1 : 0,
                (c->swallowKey_ == VK_RETURN || c->inputOpen_) ? "BLOCKED" : "passed");
        // The key that closed our line (Enter submits, Escape cancels) stays ours until it is released:
        // its auto-repeat and key-up would otherwise reach the game as confirm / target.
        if (c->swallowKey_ != 0 && w == c->swallowKey_)
        {
            if (keyUp)
            {
                c->swallowKey_       = 0;
                c->enterBlockUntil_  = ::GetTickCount64() + 400; // let DirectInput catch up with the release
            }
            return TRUE;
        }
        if (c->inputOpen_)
        {
            // Escape is handled here too, so the line can always be closed even if ImGui misses it.
            if (w == VK_ESCAPE && !keyUp)
            {
                c->inputOpen_   = false;
                c->inputBuf_[0] = 0;
                c->swallowKey_  = VK_ESCAPE;
            }
            if (w == VK_RETURN && !keyUp)
            {
                c->swallowKey_ = VK_RETURN; // ImGui submits this frame; hold Enter until it's released
                c->submitAt_   = ::GetTickCount64();
            }
            return TRUE; // our line is focused: nothing reaches the game (ImGui reads its own window messages)
        }
        auto* cm = c->core_->GetChatManager();
        if (cm != nullptr && cm->IsInputOpen() != 0)
            return FALSE; // native bar already open
        if ((::GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0)
            return FALSE; // Ctrl/Alt: game macro palette etc.
        if (auto* gui = c->core_->GetGuiManager())
            if (gui->GetIO().WantTextInput)
                return FALSE; // another ImGui text field is being typed into
        const bool enter = c->keyEnter_ && w == VK_RETURN;
        const bool space = c->keySpace_ && w == VK_SPACE;
        const bool slash = c->keySlash_ && (w == VK_OEM_2 || w == VK_DIVIDE);
        if (!enter && !space && !slash)
            return FALSE;
        if (!keyUp)
        {
            c->inputOpen_            = true;
            c->inputFocus_           = true;
            c->inputCursorEnd_       = true;
            c->inputUnfocusedFrames_ = 0;
            c->historyPos_           = -1;
            c->inputBuf_[0] = slash ? '/' : 0;
            c->inputBuf_[1] = 0;
        }
        return TRUE;
    }
    // Enter belongs to our line while it's open, until the key that closed it is released, and for a
    // short moment after (DirectInput may still report it held).
    auto ChatModule::EnterBlocked(void) -> bool
    {
        const ChatModule* c = g_chat;
        if (c == nullptr)
            return false;
        const ULONGLONG now = ::GetTickCount64();
        if (c->inputOpen_ || c->swallowKey_ == VK_RETURN || now < c->enterBlockUntil_)
            return true;
        // Just submitted and the key is still physically down (window messages can lag DirectInput).
        return c->submitAt_ != 0 && now - c->submitAt_ < 3000 && (::GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    }
    // Enter trace for /fpui chat enterlog (one line per event, with a millisecond tick).
    void ChatModule::EnterLog(const char* fmt, ...)
    {
        const ChatModule* c = g_chat;
        if (c == nullptr || !c->enterLog_ || c->enterLogFile_.empty())
            return;
        char msg[256]{};
        va_list ap;
        va_start(ap, fmt);
        ::vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);
        FILE* f = nullptr;
        if (::fopen_s(&f, c->enterLogFile_.c_str(), "ab") == 0 && f != nullptr)
        {
            ::fprintf(f, "%llu  %s\r\n", static_cast<unsigned long long>(::GetTickCount64()), msg);
            ::fclose(f);
        }
    }
    auto __stdcall ChatModule::DeviceStateCallback(LPDIRECTINPUTDEVICE8A, DWORD size, LPVOID data) -> HRESULT
    {
        // 256-byte keyboard state: clear Enter (0x1C) and keypad Enter (0x9C).
        if (data != nullptr && size >= 256)
        {
            auto* keys = static_cast<uint8_t*>(data);
            const bool held = (keys[0x1C] & 0x80) != 0 || (keys[0x9C] & 0x80) != 0;
            const bool block = EnterBlocked();
            if (held)
                EnterLog("DI state Enter held -> %s", block ? "CLEARED" : "passed");
            if (block)
            {
                keys[0x1C] = 0;
                keys[0x9C] = 0;
            }
        }
        return DI_OK;
    }
    auto __stdcall ChatModule::DeviceDataCallback(LPDIRECTINPUTDEVICE8A, DWORD objectSize, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD, DWORD) -> HRESULT
    {
        // Buffered key events: turn Enter presses into releases so the game never acts on them.
        if (data != nullptr && count != nullptr && objectSize >= sizeof(DIDEVICEOBJECTDATA))
        {
            const bool block = EnterBlocked();
            for (DWORD i = 0; i < *count; ++i)
            {
                auto* e = reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<uint8_t*>(data) + i * objectSize);
                if (e->dwOfs != 0x1C && e->dwOfs != 0x9C)
                    continue;
                EnterLog("DI event Enter %s -> %s", (e->dwData & 0x80) != 0 ? "press" : "release", block ? "CLEARED" : "passed");
                if (block)
                    e->dwData = 0;
            }
        }
        return DI_OK;
    }
    auto ChatModule::InputCallback(ImGuiInputTextCallbackData* d) -> int
    {
        auto* c = static_cast<ChatModule*>(d->UserData);
        if (c == nullptr)
            return 0;
        if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways)
        {
            // Focusing the field selects all of it; the first typed key would replace the prefilled "/".
            if (c->inputCursorEnd_)
            {
                d->CursorPos      = d->BufTextLen;
                d->SelectionStart = d->SelectionEnd = d->BufTextLen;
                c->inputCursorEnd_ = false;
            }
            return 0;
        }
        if (d->EventFlag != ImGuiInputTextFlags_CallbackHistory || c->inputHistory_.empty())
            return 0;
        const int n = static_cast<int>(c->inputHistory_.size());
        if (d->EventKey == ImGuiKey_UpArrow)
            c->historyPos_ = c->historyPos_ < 0 ? n - 1 : std::max(0, c->historyPos_ - 1);
        else if (d->EventKey == ImGuiKey_DownArrow)
            c->historyPos_ = c->historyPos_ < 0 ? -1 : (c->historyPos_ + 1 >= n ? -1 : c->historyPos_ + 1);
        const std::string& t = c->historyPos_ >= 0 ? c->inputHistory_[static_cast<size_t>(c->historyPos_)] : std::string();
        const int len = std::min(static_cast<int>(t.size()), d->BufSize - 1);
        std::memcpy(d->Buf, t.data(), static_cast<size_t>(len));
        d->Buf[len]       = 0;
        d->BufTextLen     = len;
        d->CursorPos      = len;
        d->SelectionStart = d->SelectionEnd = len;
        d->BufDirty       = true;
        return 0;
    }
    void ChatModule::RenderInput(Context& ctx, IGuiManager* gui)
    {
        // Native bar opened through another path (macro text, other addon): no SDK call closes it,
        // so just note it once; our line stays available.
        if (auto* cm = core_ != nullptr ? core_->GetChatManager() : nullptr)
        {
            const bool open = cm->IsInputOpen() != 0;
            if (open && !nativeSeen_)
                ctx.Print("FarplaneUI: native input bar opened by another path (it cannot be closed from a plugin).");
            nativeSeen_ = open;
        }
        const bool em = ember::On();
        if (!inputOpen_)
        {
            if (em)
            {
                const auto& t  = ember::T();
                const ImVec2 p = gui->GetCursorScreenPos();
                const float w = gui->GetContentRegionAvail().x, h = t.S(t.inputH);
                ember::Track(ctx, gui, p, ImVec2(w, h));
                ember::Text(gui, ImVec2(p.x + t.S(12), p.y + (h - gui->GetTextLineHeight()) * 0.5f), t.dim, "Press Space or / to chat");
                gui->SetCursorScreenPos(p);
                gui->Dummy(ImVec2(w, h));
                return;
            }
            gui->TextDisabled("Press Space or / to chat");
            return;
        }
        if (gui->IsKeyPressed(ImGuiKey_Escape, false))
        {
            inputOpen_ = false;
            inputBuf_[0] = 0;
            return;
        }
        int emStyles = 0, emColors = 0;
        if (em)
        {
            const auto& t = ember::T();
            gui->PushStyleVar(ImGuiStyleVar_FrameRounding, t.S(t.inputH) * 0.5f);
            gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            gui->PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(t.S(12), std::max(1.0f, (t.S(t.inputH) - gui->GetTextLineHeight()) * 0.5f)));
            gui->PushStyleColor(ImGuiCol_FrameBg, ToVec4(t.track));
            gui->PushStyleColor(ImGuiCol_Border, ToVec4(t.edge));
            emStyles = 3, emColors = 2;
        }
        else
        {
            gui->TextColored(ToVec4(art::ember), ">");
            gui->SameLine();
        }
        gui->PushItemWidth(-1.0f);
        if (inputFocus_)
        {
            gui->SetKeyboardFocusHere(0);
            inputFocus_ = false;
        }
        const bool enter = gui->InputText("##fpui_chat_input", inputBuf_, sizeof(inputBuf_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways,
            &ChatModule::InputCallback, this);
        const bool lostFocus = !enter && gui->IsItemDeactivated();
        // Watchdog: every key is blocked from the game while the line is open, so never let it
        // stay open without keyboard focus.
        inputUnfocusedFrames_ = gui->IsItemActive() ? 0 : inputUnfocusedFrames_ + 1;
        gui->PopItemWidth();
        if (emColors > 0)
            gui->PopStyleColor(emColors);
        if (emStyles > 0)
            gui->PopStyleVar(emStyles);
        if (inputUnfocusedFrames_ > 10)
        {
            inputOpen_   = false;
            inputBuf_[0] = 0;
            ctx.Print("FarplaneUI: chat line could not take keyboard focus and was closed.");
            return;
        }
        if (enter)
        {
            std::string text(inputBuf_);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
                text.pop_back();
            if (!text.empty() && core_ != nullptr && core_->GetChatManager() != nullptr)
            {
                // -1 = CommandMode::AshitaParse: Ashita's plugins/addons see it first, then the game
                // (same as typing into the chat bar; XIUI uses this too). /fpui is handled directly.
                const std::string low = ui::Lower(text.substr(0, 6));
                bool handled = false;
                if ((low == "/fpui" || low == "/fpui ") && ctx.commandFn != nullptr && ctx.host != nullptr)
                    handled = ctx.commandFn(ctx.host, text.c_str());
                if (!handled)
                    core_->GetChatManager()->QueueCommand(-1, text.c_str());
                if (inputHistory_.empty() || inputHistory_.back() != text)
                    inputHistory_.push_back(text);
                if (inputHistory_.size() > 100)
                    inputHistory_.erase(inputHistory_.begin());
            }
            inputBuf_[0] = 0;
            inputOpen_   = false;
            submitAt_    = ::GetTickCount64();
            EnterLog("submitted \"%s\"", text.c_str());
        }
        else if (lostFocus)
            inputOpen_ = false; // clicked away
    }

    void ChatModule::Direct3DInit(Context& ctx)
    {
        font_ = nullptr;
        if (fontFile_.empty())
        {
            fontStatus_ = "default font";
            return;
        }
        const std::string path = ctx.resourceDir + "fonts\\" + fontFile_;
        if (::GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            fontStatus_ = "font not found: " + path + " (using the default font)";
            return;
        }
        auto* gui = ctx.core->GetGuiManager();
        font_     = gui != nullptr ? gui->AddFontFromFileTTF(path.c_str(), fontSize_, nullptr) : nullptr;
        fontStatus_ = font_ != nullptr ? "loaded " + path : "AddFontFromFileTTF failed for " + path + " (using the default font)";
    }

    void ChatModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        constexpr const char* S = "chat";
        mod::LoadDock(cfg, alias, S, show_, docked_, locked_, user_);
        hideNative_ = cfg->GetBool(alias, S, "hideNative", hideNative_);
        inputEnabled_ = cfg->GetBool(alias, S, "input", inputEnabled_);
        openKeys_     = ui::GetS(cfg, alias, S, "openKeys", openKeys_);
        ParseOpenKeys();
        timestamps_ = cfg->GetBool(alias, S, "timestamps", timestamps_);
        tsFormat_   = ui::GetS(cfg, alias, S, "timestampFormat", tsFormat_);
        autoScroll_ = cfg->GetBool(alias, S, "autoScroll", autoScroll_);
        opacity_    = std::clamp(cfg->GetFloat(alias, S, "opacity", opacity_), 0.0f, 1.0f);
        fontSize_   = std::clamp(cfg->GetFloat(alias, S, "fontSize", fontSize_), 8.0f, 40.0f);
        lineGap_    = std::clamp(cfg->GetFloat(alias, S, "lineGap", lineGap_), 0.0f, 12.0f);
        fontFile_   = ui::GetS(cfg, alias, S, "fontFile", fontFile_);
        if (fontFile_ == "default")
            fontFile_.clear();
        history_  = std::clamp(cfg->GetInt32(alias, S, "history", history_), 100, 20000);
        artFrame_ = cfg->GetBool(alias, S, "artFrame", artFrame_);
        yieldToMenus_ = cfg->GetBool(alias, S, "yieldToMenus", yieldToMenus_);
        TabsFromString(ui::GetS(cfg, alias, S, "tabs", TabsToString()));
        activeTab_ = std::clamp(cfg->GetInt32(alias, S, "activeTab", 0), 0, static_cast<int>(tabs_.size()) - 1);
        DefaultColors();
        for (int i = 0; i < chat::kCatCount; ++i)
        {
            Rgba c{};
            if (ui::HexToColor(ui::GetS(cfg, alias, S, (std::string("color_") + chat::kCatNames[i]).c_str(), ""), c))
                colors_[static_cast<size_t>(i)] = c;
        }
    }

    void ChatModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        constexpr const char* S = "chat";
        mod::SaveDock(cfg, alias, S, show_, docked_, locked_, user_);
        ui::SetB(cfg, alias, S, "hideNative", hideNative_);
        ui::SetB(cfg, alias, S, "input", inputEnabled_);
        cfg->SetValue(alias, S, "openKeys", openKeys_.c_str());
        ui::SetB(cfg, alias, S, "timestamps", timestamps_);
        cfg->SetValue(alias, S, "timestampFormat", tsFormat_.c_str());
        ui::SetB(cfg, alias, S, "autoScroll", autoScroll_);
        ui::SetF(cfg, alias, S, "opacity", opacity_);
        ui::SetF(cfg, alias, S, "fontSize", fontSize_);
        ui::SetF(cfg, alias, S, "lineGap", lineGap_);
        cfg->SetValue(alias, S, "fontFile", fontFile_.empty() ? "default" : fontFile_.c_str());
        ui::SetI(cfg, alias, S, "history", history_);
        ui::SetB(cfg, alias, S, "artFrame", artFrame_);
        ui::SetB(cfg, alias, S, "yieldToMenus", yieldToMenus_);
        cfg->SetValue(alias, S, "tabs", TabsToString().c_str());
        ui::SetI(cfg, alias, S, "activeTab", activeTab_);
        for (int i = 0; i < chat::kCatCount; ++i)
            cfg->SetValue(alias, S, (std::string("color_") + chat::kCatNames[i]).c_str(), ui::ColorToHex(colors_[static_cast<size_t>(i)]).c_str());
    }

    auto ChatModule::IncomingText(Context& ctx, int32_t mode, const char* message, bool injected) -> bool
    {
        (void)injected;
        if (message == nullptr || message[0] == '\0')
            return false;
        const auto now   = std::time(nullptr);
        const Cat cat    = chat::Categorize(mode);
        auto decoded     = chat::Decode(ctx.core->GetChatManager(), message);
        // NPC dialogue (modes 150 / 151) is sent again as the dialogue box pages, so the same line
        // arrived 2-3 times. Drop an NPC line identical to one shown in the last few seconds; other
        // modes are never deduplicated (two identical hits are two real hits).
        const int npcMode = static_cast<int>(static_cast<uint32_t>(mode) & 0xFFu);
        // NPC lines also come in on other modes (the shop / chocobo NPC lines did): dedupe every line
        // the chat files under NPC or System, never battle or player chat.
        const bool npc    = npcMode == 150 || npcMode == 151 || cat == Cat::Npc || cat == Cat::System;
        const ULONGLONG tick = ::GetTickCount64();
        while (!npcRecent_.empty() && tick - npcRecent_.front().second > 5000)
            npcRecent_.pop_front();
        for (auto& segs : decoded)
        {
            Line l;
            l.seq   = ++seq_;
            l.time  = now;
            l.mode  = mode;
            l.cat   = cat;
            l.plain = chat::PlainText(segs);
            if (l.plain.empty())
                continue;
            if (npc)
            {
                bool dup = false;
                for (const auto& [text, at] : npcRecent_)
                    dup |= text == l.plain;
                if (dup)
                    continue;
                npcRecent_.emplace_back(l.plain, tick);
            }
            l.segs = std::move(segs);
            lines_.push_back(std::move(l));
        }
        while (lines_.size() > static_cast<size_t>(history_))
            lines_.pop_front();

        const int m = static_cast<int>(static_cast<uint32_t>(mode) & 0xFFu);
        return hideNative_ && show_ && m != 150 && m != 151;
    }

    auto ChatModule::Visible(const Line& l, const Tab& t) const -> bool
    {
        return (t.mask & Bit(l.cat)) != 0;
    }

    void ChatModule::DrawLine(IGuiManager* gui, const Context& ctx, const Line& l, float wrapWidth)
    {
        (void)ctx;
        const Rgba& base = colors_[static_cast<size_t>(l.cat)];
        const float startX = gui->GetCursorPosX();
        float x            = 0.0f;
        bool first         = true;
        auto emit = [&](const std::string& word, const Rgba& col) {
            const float w = gui->CalcTextSize(word.c_str()).x;
            if (!first)
            {
                if (x + w > wrapWidth && x > 0.0f && word != " ")
                {
                    // The previous word already moved the cursor to the next line (no SameLine), so no
                    // NewLine here - that added a blank line under every wrapped line.
                    gui->SetCursorPosX(startX + 12.0f); // hanging indent
                    x = 12.0f;
                }
                else
                {
                    gui->SameLine(0.0f, 0.0f);
                }
            }
            gui->TextColored(ToVec4(col), "%s", word.c_str());
            x += w;
            first = false;
        };
        auto emitText = [&](const std::string& text, const Rgba& col) {
            // split into words keeping the spaces attached so measuring stays exact
            size_t i = 0;
            while (i < text.size())
            {
                size_t j = text.find(' ', i);
                j        = (j == std::string::npos) ? text.size() : j + 1;
                emit(text.substr(i, j - i), col);
                i = j;
            }
        };
        if (timestamps_ && !tsFormat_.empty())
        {
            std::tm tm{};
            ::localtime_s(&tm, &l.time);
            char buf[64]{};
            if (std::strftime(buf, sizeof(buf), tsFormat_.c_str(), &tm) > 0)
                emit(std::string(buf) + " ", Rgba{0.6f, 0.58f, 0.55f, 0.9f});
        }
        for (const auto& s : l.segs)
        {
            const Rgba& col = s.color == 1 ? art::gold_hi : s.color == 2 ? art::orb_cyan : s.color == 3 ? kAutoTranslate : base;
            emitText(s.text, col);
        }
        if (first)
            gui->NewLine();
    }

    auto ChatModule::CopyTab(int maxLines) const -> std::string
    {
        if (tabs_.empty())
            return {};
        const Tab& t = tabs_[static_cast<size_t>(activeTab_)];
        std::vector<const Line*> sel;
        for (auto it = lines_.rbegin(); it != lines_.rend() && (maxLines <= 0 || static_cast<int>(sel.size()) < maxLines); ++it)
            if (Visible(*it, t))
                sel.push_back(&*it);
        std::string out;
        for (auto it = sel.rbegin(); it != sel.rend(); ++it)
        {
            if (timestamps_)
            {
                std::tm tm{};
                ::localtime_s(&tm, &(*it)->time);
                char buf[64]{};
                if (std::strftime(buf, sizeof(buf), tsFormat_.c_str(), &tm) > 0)
                    out += std::string(buf) + " ";
            }
            out += (*it)->plain + "\r\n";
        }
        return out;
    }

    void ChatModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        // Experiment: hold the command menu's x copies (+0x3A, +0x52, +0x8C) every frame while it's open.
        if (holdMenuX_ >= 0 && mod::TopMenuName() == "menu    playermo")
            if (const uintptr_t o = mod::TopMenuObject(); o != 0)
                for (const uintptr_t off : {0x3Au, 0x52u, 0x8Cu})
                {
                    DWORD old = 0;
                    if (::VirtualProtect(reinterpret_cast<void*>(o + off), 2, PAGE_READWRITE, &old))
                    {
                        *reinterpret_cast<volatile int16_t*>(o + off) = static_cast<int16_t>(holdMenuX_);
                        ::VirtualProtect(reinterpret_cast<void*>(o + off), 2, old, &old);
                    }
                }
        // Step aside while a native game menu (command menu, trade, item lists...) is open: those draw
        // in the bottom-left where the docked chat sits, and ImGui always draws over the game UI.
        if (yieldToMenus_ && !inputOpen_)
        {
            const std::string menu = mod::TopMenuName();
            // The command menu and its lists are moved to their own frame when that's on.
            const bool moved = ctx.commandMenuMoved &&
                               (menu == "menu    playermo" || menu == "menu    magic" || menu == "menu    ability" || menu == "menu    magselec" ||
                                menu == "menu    abiselec" || menu == "menu    mount");
            if (!menu.empty() && !moved)
                return;
        }
        // The outer panel never scrolls (only the message log inside does): a wheel over the tabs used to
        // scroll the whole panel, pushing the tab row out and the lines over the frame.
        auto p = mod::BeginPanel(ctx, gui, "##fpui_chat", nullptr, docked_, ctx.layout.chat, user_, locked_, opacity_, artFrame_,
            ImGuiWindowFlags_NoScrollWithMouse);
        if (p.open)
        {
            gui->SetScrollY(0.0f);
            gui->PushFont(font_, fontSize_); // nullptr = current font at our size (imgui 1.92 dynamic fonts)
            // Tabs (Ember: rounded pills; ember when active)
            if (ember::On())
            {
                const auto& t = ember::T();
                gui->PushFont(nullptr, std::max(10.0f, t.S(12)));
                gui->PushStyleVar(ImGuiStyleVar_FrameRounding, t.S(t.tabH) * 0.5f);
                gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                gui->PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(t.S(t.tabPadX), std::max(0.0f, (t.S(t.tabH) - gui->GetTextLineHeight()) * 0.5f)));
                for (size_t i = 0; i < tabs_.size(); ++i)
                {
                    const bool active = static_cast<int>(i) == activeTab_;
                    gui->PushStyleColor(ImGuiCol_Button, ToVec4(active ? t.ember : t.tabIdle));
                    gui->PushStyleColor(ImGuiCol_ButtonHovered, ToVec4(active ? t.ember : Rgba{t.ember.r, t.ember.g, t.ember.b, 0.45f}));
                    gui->PushStyleColor(ImGuiCol_ButtonActive, ToVec4(t.ember));
                    gui->PushStyleColor(ImGuiCol_Border, ToVec4(active ? Rgba{0, 0, 0, 0} : t.edgeDim));
                    gui->PushStyleColor(ImGuiCol_Text, ToVec4(active ? t.ivory : t.muted));
                    if (i > 0)
                        gui->SameLine(0.0f, t.S(t.tabGap));
                    gui->PushID(static_cast<int>(i));
                    if (gui->Button(tabs_[i].name.c_str()) && !active)
                    {
                        activeTab_      = static_cast<int>(i);
                        scrollToBottom_ = true;
                        ctx.MarkDirty();
                    }
                    gui->PopID();
                    gui->PopStyleColor(5);
                }
                gui->PopStyleVar(3);
                gui->PopFont();
                gui->Dummy(ImVec2(0, t.S(2)));
            }
            else
            {
            gui->PushStyleColor(ImGuiCol_Tab, ToVec4(Rgba{art::navy.r, art::navy.g, art::navy.b, 0.8f}));
            gui->PushStyleColor(ImGuiCol_TabHovered, ToVec4(art::ember));
            gui->PushStyleColor(ImGuiCol_TabSelected, ToVec4(Rgba{art::ember.r, art::ember.g, art::ember.b, 0.85f}));
            gui->PushStyleColor(ImGuiCol_TabSelectedOverline, ToVec4(art::gold_hi));
            if (gui->BeginTabBar("##fpui_chat_tabs", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                for (size_t i = 0; i < tabs_.size(); ++i)
                {
                    gui->PushID(static_cast<int>(i));
                    if (gui->BeginTabItem(tabs_[i].name.c_str()))
                    {
                        if (activeTab_ != static_cast<int>(i))
                        {
                            activeTab_      = static_cast<int>(i);
                            scrollToBottom_ = true;
                            ctx.MarkDirty();
                        }
                        gui->EndTabItem();
                    }
                    gui->PopID();
                }
                gui->EndTabBar();
            }
            gui->PopStyleColor(4);
            }
            if (scrollLock_)
            {
                gui->SameLine();
                gui->TextColored(ToVec4(art::ember), "[scroll lock]");
            }

            gui->PushStyleColor(ImGuiCol_ChildBg, ToVec4(Rgba{0, 0, 0, 0}));
            gui->PushStyleColor(ImGuiCol_ScrollbarBg, ToVec4(Rgba{0, 0, 0, 0}));
            gui->PushStyleColor(ImGuiCol_ScrollbarGrab, ToVec4(Rgba{art::ember.r, art::ember.g, art::ember.b, 0.5f}));
            const float inputH = !inputEnabled_ ? 0.0f : ember::On() ? ember::T().S(ember::T().inputH) + gui->GetStyle().ItemSpacing.y : gui->GetFrameHeightWithSpacing();
            gui->PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gui->GetStyle().ItemSpacing.x, lineGap_)); // gap between chat lines
            const bool linesOpen = gui->BeginChild("##fpui_chat_lines", ImVec2(0, -inputH), 0, 0);
            gui->PopStyleVar();
            if (linesOpen)
            {
                gui->PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gui->GetStyle().ItemSpacing.x, lineGap_));
                const bool atBottom = gui->GetScrollY() >= gui->GetScrollMaxY() - 4.0f;
                const float wrap    = gui->GetContentRegionAvail().x - 4.0f;
                if (!tabs_.empty())
                {
                    // Virtualised: only lines inside the scroll view are drawn. Each line's height is
                    // measured when it is drawn (cached per wrap width); lines never drawn yet use an
                    // estimate. Off-screen lines just advance the cursor.
                    const Tab& t      = tabs_[static_cast<size_t>(activeTab_)];
                    const float viewT = gui->GetScrollY() - 40.0f, viewB = gui->GetScrollY() + gui->GetWindowHeight() + 40.0f;
                    const float lineH = gui->GetTextLineHeightWithSpacing();
                    float y           = gui->GetCursorPosY();
                    for (const auto& l : lines_)
                    {
                        if (!Visible(l, t))
                            continue;
                        float h = l.heightWrap == wrap && l.height > 0 ? l.height : 0.0f;
                        if (h <= 0)
                        {
                            const float tw = gui->CalcTextSize(l.plain.c_str(), nullptr, false, -1.0f).x + (timestamps_ ? 70.0f : 0.0f);
                            h = lineH * std::max(1.0f, std::ceil(tw / std::max(40.0f, wrap)));
                        }
                        if (y + h < viewT || y > viewB)
                        {
                            y += h;
                            continue;
                        }
                        gui->SetCursorPosY(y);
                        DrawLine(gui, ctx, l, wrap);
                        const float measured = gui->GetCursorPosY() - y;
                        l.height = measured, l.heightWrap = wrap;
                        y += measured;
                    }
                    gui->SetCursorPosY(y);
                    gui->Dummy(ImVec2(0, 0));
                }
                if (scrollToBottom_ || (autoScroll_ && !scrollLock_ && atBottom))
                    gui->SetScrollHereY(1.0f);
                scrollToBottom_ = false;

                if (gui->BeginPopupContextWindow("##fpui_chat_ctx"))
                {
                    if (gui->MenuItem("Copy this tab"))
                        gui->SetClipboardText(CopyTab(0).c_str());
                    if (gui->MenuItem("Copy last 20 lines"))
                        gui->SetClipboardText(CopyTab(20).c_str());
                    if (gui->MenuItem("Scroll lock", nullptr, scrollLock_))
                        scrollLock_ = !scrollLock_;
                    if (gui->MenuItem("Jump to bottom"))
                        scrollToBottom_ = true;
                    if (gui->MenuItem("Clear history"))
                        lines_.clear();
                    gui->EndPopup();
                }
                gui->PopStyleVar(); // line gap
            }
            gui->EndChild();
            gui->PopStyleColor(3);
            if (inputEnabled_)
                RenderInput(ctx, gui);
            gui->PopFont();
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }
    auto ChatModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (mod::DockCommand(ctx, "chat", args, show_, docked_, locked_))
            return true;
        const std::string sub = ui::Lower(args[0]);
        const std::string a1  = args.size() > 1 ? ui::Lower(args[1]) : std::string();
        int n = 0;
        float f = 0;
        if (sub == "menu")
        {
            const std::string m = mod::TopMenuName();
            ctx.Print(m.empty() ? "Chat: no game menu open." : "Chat: open game menu is \"" + m + "\".");
            // "/fpui chat menu dump": the header's first 0x60 bytes as signed 16-bit values (offset: value),
            // to find the menu's on-screen position.
            // "/fpui chat menu set <hex offset> <value>": write one signed 16-bit value into the open
            // menu's header (experiment to find its position fields).
            // "/fpui chat menu obj set|dump ...": the same on the live menu object instead of its header.
            const bool obj = args.size() > 1 && ui::Lower(args[1]) == "obj";
            std::vector<std::string> a(args);
            if (obj)
                a.erase(a.begin() + 1);
            const uintptr_t target = obj ? mod::TopMenuObject() : mod::TopMenuHeader();
            if (a.size() > 3 && ui::Lower(a[1]) == "set")
                if (const uintptr_t h = target; h != 0)
                {
                    const auto off = static_cast<uintptr_t>(std::strtoul(a[2].c_str(), nullptr, 16));
                    const auto val = static_cast<int16_t>(std::atoi(a[3].c_str()));
                    if (off < 0xC0)
                    {
                        DWORD old = 0;
                        if (::VirtualProtect(reinterpret_cast<void*>(h + off), 2, PAGE_READWRITE, &old))
                        {
                            *reinterpret_cast<volatile int16_t*>(h + off) = val;
                            ::VirtualProtect(reinterpret_cast<void*>(h + off), 2, old, &old);
                            ctx.Print("Chat: wrote " + std::to_string(val) + " at +0x" + a[2] + ".");
                        }
                    }
                }
            // "/fpui chat menu hold <x>|off": keep the command menu's x pinned every frame (experiment).
            if (a.size() > 2 && ui::Lower(a[1]) == "hold")
            {
                holdMenuX_ = ui::Lower(a[2]) == "off" ? -1 : std::atoi(a[2].c_str());
                ctx.Print(holdMenuX_ < 0 ? "Chat: menu hold off." : "Chat: holding command menu x at " + std::to_string(holdMenuX_) + ".");
            }
            if (a.size() > 1 && ui::Lower(a[1]) == "dump")
                if (const uintptr_t h = target; h != 0)
                    for (int row = 0; row < (obj ? 8 : 4); ++row)
                    {
                        std::string line = "  ";
                        for (int i = 0; i < 12; ++i)
                        {
                            const int off = (row * 12 + i) * 2;
                            char b[24]{};
                            ::sprintf_s(b, "%02X:%d ", off, static_cast<int>(Ashita::Memory::SafeRead<int16_t>(h + static_cast<uintptr_t>(off), static_cast<int16_t>(0))));
                            line += b;
                        }
                        ctx.Print(line);
                    }
            return true;
        }
        if (sub == "enterlog")
        {
            enterLog_ = !enterLog_;
            if (enterLog_)
            {
                const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
                ::CreateDirectoryA(dir.c_str(), nullptr);
                enterLogFile_ = dir + "\\farplaneui_enter.log";
                ::DeleteFileA(enterLogFile_.c_str());
                ctx.Print("Chat: logging Enter to " + enterLogFile_ + ". Type /fish (or anything) and press Enter, then /fpui chat enterlog again.");
            }
            else
                ctx.Print("Chat: Enter log stopped: " + enterLogFile_);
            return true;
        }
        if (sub == "input")

        {
            inputEnabled_ = (a1 == "on") ? true : (a1 == "off") ? false : !inputEnabled_;
            if (!inputEnabled_)
                inputOpen_ = false;
            ctx.Print(inputEnabled_ ? "FarplaneUI chat input ON (Enter / Space / '/' open it; Ctrl/Alt macros unaffected)." : "FarplaneUI chat input OFF (native input bar used).");
        }
        else if (sub == "hidenative")
        {
            hideNative_ = (a1 == "on") ? true : (a1 == "off") ? false : !hideNative_;
            ctx.Print(hideNative_ ? "Native chat log hidden (lines are blocked; NPC dialogue 150/151 always passes)." : "Native chat log shown.");
        }
        else if (sub == "timestamps" || sub == "ts")
        {
            if (a1 == "format" && args.size() > 2)
            {
                tsFormat_ = ui::Join(args, 2);
                ctx.Print("Timestamp format: " + tsFormat_);
            }
            else
            {
                timestamps_ = (a1 == "on") ? true : (a1 == "off") ? false : !timestamps_;
                ctx.Print(timestamps_ ? "Timestamps on." : "Timestamps off.");
            }
        }
        else if (sub == "opacity" && args.size() > 1 && ui::ParseFloat(args[1], f))
        {
            opacity_ = std::clamp(f, 0.0f, 1.0f);
            ctx.Print("Chat background opacity set.");
        }
        else if (sub == "font")
        {
            if (a1 == "size" && args.size() > 2 && ui::ParseFloat(args[2], f))
            {
                fontSize_ = std::clamp(f, 8.0f, 40.0f);
                ctx.Print("Chat font size set (applies now; a new TTF file needs a reload).");
            }
            else if (args.size() > 1)
            {
                fontFile_ = (a1 == "default") ? std::string() : ui::Join(args, 1);
                Direct3DInit(ctx);
                ctx.Print("Chat font: " + fontStatus_);
            }
            else
                ctx.Print("Chat font: " + fontStatus_ + ", size " + std::to_string(static_cast<int>(fontSize_)));
        }
        else if (sub == "scroll")
        {
            if (a1 == "lock" || a1 == "unlock")
                scrollLock_ = (a1 == "lock");
            else if (a1 == "auto")
                autoScroll_ = !autoScroll_;
            else
                scrollToBottom_ = true;
            ctx.Print(std::string("Scroll lock ") + (scrollLock_ ? "on" : "off") + ", auto-scroll " + (autoScroll_ ? "on" : "off") + ".");
            return true;
        }
        else if (sub == "copy")
        {
            const int count = (args.size() > 1 && ui::ParseInt(args[1], n)) ? n : 20;
            const std::string text = CopyTab(count);
            if (auto* gui = ctx.core->GetGuiManager())
                gui->SetClipboardText(text.c_str());
            ctx.Print("Copied " + std::to_string(count) + " lines of the current tab to the clipboard.");
            return true;
        }
        else if (sub == "clear")
        {
            lines_.clear();
            ctx.Print("Chat history cleared.");
            return true;
        }
        else if (sub == "history" && args.size() > 1 && ui::ParseInt(args[1], n))
        {
            history_ = std::clamp(n, 100, 20000);
            ctx.Print("History: " + std::to_string(history_) + " lines.");
        }
        else if (sub == "tab")
        {
            // /fpui chat tab list | add <name> <cat,cat|*> | del <name> | set <name> <cat,...> | reset
            if (a1 == "list" || a1.empty())
            {
                ctx.Print("Tabs: " + TabsToString());
                std::string cats;
                for (const auto* c : chat::kCatNames)
                    cats += (cats.empty() ? "" : ", ") + std::string(c);
                ctx.Print("Categories: " + cats + " (* = all)");
                return true;
            }
            if (a1 == "reset")
                DefaultTabs();
            else if ((a1 == "add" || a1 == "set") && args.size() > 3)
            {
                const std::string name = args[2];
                Tab t{name, 0};
                std::vector<Tab> keep = tabs_;
                TabsFromString(name + ":" + args[3]);
                t.mask = tabs_.front().mask;
                tabs_  = keep;
                auto it = std::find_if(tabs_.begin(), tabs_.end(), [&](const Tab& x) { return _stricmp(x.name.c_str(), name.c_str()) == 0; });
                if (it != tabs_.end())
                    it->mask = t.mask;
                else
                    tabs_.push_back(t);
            }
            else if (a1 == "del" && args.size() > 2)
            {
                const std::string name = args[2];
                std::erase_if(tabs_, [&](const Tab& x) { return _stricmp(x.name.c_str(), name.c_str()) == 0; });
                if (tabs_.empty())
                    DefaultTabs();
            }
            else
            {
                ctx.Print("Usage: /fpui chat tab list | add <name> <cat,cat|*> | set <name> <cats> | del <name> | reset");
                return true;
            }
            activeTab_ = std::clamp(activeTab_, 0, static_cast<int>(tabs_.size()) - 1);
            ctx.Print("Tabs: " + TabsToString());
        }
        else if (sub == "color" && args.size() > 2)
        {
            Cat c{};
            Rgba col{};
            if (!chat::CatFromName(a1, c) || !ui::HexToColor(args[2], col))
            {
                ctx.Print("Usage: /fpui chat color <category> <RRGGBB>");
                return true;
            }
            colors_[static_cast<size_t>(c)] = col;
        }
        else if (sub == "frame")
        {
            artFrame_ = !artFrame_;
        }
        else
        {
            ctx.Print("/fpui chat [show|hide|dock|undock|lock|unlock] | hidenative [on|off] | ts [on|off|format <strftime>] | "
                      "opacity <0-1> | font <file.ttf|default> | font size <n> | scroll [lock|unlock|auto] | copy [n] | clear | "
                      "history <n> | tab ... | color <cat> <RRGGBB> | frame");
            return true;
        }
        ctx.MarkDirty();
        return true;
    }

    void ChatModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        const Palette& c = *ctx.palette;
        bool ch = false;
        cfg::Grid g(gui, ctx);
        switch (ctx.settingsSection)
        {
        case 0: // General
        {
            mod::DockSettings(g, ctx, show_, docked_, locked_);
            ch |= g.Toggle("Hide native chat log", "Blocks lines from the game's own log while this window shows (NPC dialogue always passes).", hideNative_);
            ch |= g.Toggle("FarplaneUI input line", "Type into FarplaneUI's own input line instead of the game's.", inputEnabled_);
            bool kc = g.Toggle("Space opens input", "Space opens the input line.", keySpace_);
            kc |= g.Toggle("/ opens input", "Slash opens the input line, prefilled with /.", keySlash_);
            if (kc)
            {
                openKeys_.clear();
                if (keySpace_) openKeys_ += "space,";
                if (keySlash_) openKeys_ += "slash,";
                if (!openKeys_.empty()) openKeys_.pop_back();
                ch = true;
            }
            ch |= g.Toggle("Art frame", "Draw the ornate art-kit frame around the chat.", artFrame_);
            ch |= g.Toggle("Make room for game menus", "Hide the chat while a game menu (command menu, trade...) is open so it shows.", yieldToMenus_);
            break;
        }
        case 1: // Text
        {
            ch |= g.Toggle("Timestamps", "Prefix each line with the time.", timestamps_);
            ch |= g.Slider("Font size", "Chat text size.", fontSize_, 8.0f, 40.0f, "%.0f");
            ch |= g.Slider("Line spacing", "Extra pixels between chat lines (wrapped lines stay tight).", lineGap_, 0.0f, 12.0f, "%.0f");
            ch |= g.Slider("Background opacity", "Opacity of the chat background.", opacity_, 0.0f, 1.0f, "%.2f");
            ch |= g.Slider("History lines", "How many lines are kept for scrollback.", history_, 100, 20000);
            ch |= g.Toggle("Auto-scroll", "Follow new lines as they arrive.", autoScroll_);
            g.Toggle("Scroll lock", "Hold the current scroll position.", scrollLock_);
            g.Info("Font", fontStatus_.c_str());
            g.Free();
            char ts[64]{};
            ::strncpy_s(ts, tsFormat_.c_str(), _TRUNCATE);
            gui->PushItemWidth(160);
            if (gui->InputText("Timestamp format (strftime)", ts, sizeof(ts)))
            {
                tsFormat_ = ts;
                ch        = true;
            }
            gui->PopItemWidth();
            gui->TextDisabled("Change the TTF with /fpui chat font <file.ttf> (from resources\\farplaneui\\fonts).");
            break;
        }
        case 2: // Tabs
        {
            g.Free();
            if (gui->BeginTable("##fpui_tabs", chat::kCatCount + 1, ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit, ImVec2(0, 220)))
            {
                gui->TableSetupColumn("Tab");
                for (const auto* nm : chat::kCatNames)
                    gui->TableSetupColumn(nm);
                gui->TableHeadersRow();
                for (size_t t = 0; t < tabs_.size(); ++t)
                {
                    gui->TableNextRow();
                    gui->TableSetColumnIndex(0);
                    gui->TextUnformatted(tabs_[t].name.c_str());
                    for (int k = 0; k < chat::kCatCount; ++k)
                    {
                        gui->TableSetColumnIndex(k + 1);
                        bool on = (tabs_[t].mask & (1u << k)) != 0;
                        gui->PushID(static_cast<int>(t * 64 + static_cast<size_t>(k)));
                        if (gui->Checkbox("##c", &on))
                        {
                            tabs_[t].mask = on ? (tabs_[t].mask | (1u << k)) : (tabs_[t].mask & ~(1u << k));
                            ch            = true;
                        }
                        gui->PopID();
                    }
                }
                gui->EndTable();
            }
            gui->TextDisabled("Add/remove tabs: /fpui chat tab add <name> <cats|*>, /fpui chat tab del <name>");
            break;
        }
        default: // Colors
        {
            g.Free();
            (void)c;
            for (int k = 0; k < chat::kCatCount; ++k)
            {
                auto& col = colors_[static_cast<size_t>(k)];
                float v[4] = {col.r, col.g, col.b, col.a};
                if (gui->ColorEdit4(chat::kCatTitles[k], v, ImGuiColorEditFlags_NoInputs))
                {
                    col = Rgba{v[0], v[1], v[2], v[3]};
                    ch  = true;
                }
                if (k % 3 != 2)
                    gui->SameLine(static_cast<float>((k % 3 + 1) * 180));
            }
            gui->NewLine();
            if (g.Button("Reset colours", "Restore the default colour for every category.", "Reset"))
            {
                DefaultColors();
                ch = true;
            }
            break;
        }
        }
        if (ch)
            ctx.MarkDirty();
    }
} // namespace farplaneui
