/**
 * Farplane UI - FFXI chat line decoding.
 *
 * Incoming chat is Shift-JIS (code page 932) with FFXI control bytes:
 *   0x1E nn   color escape, palette 1 (nn = 0x01 resets to the channel color)
 *   0x1F nn   color escape, palette 2 (mode colors)
 *   0x7F nn   client control pair (prompts, item/key-item link markers) - dropped
 *   0xFD .. 0xFD  raw auto-translate token - expanded with IChatManager::ParseAutoTranslate
 *   0xEF 0x27 / 0xEF 0x28  auto-translate brackets - rendered as { }
 *   0xEF 0x1F..0x26        element glyphs - rendered as [Fire] .. [Dark]
 *   0x07                   soft line break - rendered as a space
 * Everything else outside ASCII is converted cp932 -> UTF-16 -> UTF-8 (MultiByteToWideChar).
 *
 * Written from the format description (FancyChat's GPL-3 tables are not copied). Inline palette
 * colors are approximated with the art kit (see README).
 */

#ifndef FARPLANEUI_CHATPARSE_HPP_INCLUDED
#define FARPLANEUI_CHATPARSE_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"

#include <cstdint>
#include <string>
#include <vector>

namespace farplaneui::chat
{
    enum class Cat : int
    {
        Say = 0, Shout, Yell, Tell, Party, Linkshell1, Linkshell2, Emote, Unity, Assist, Battle, Npc, System, Error, Count
    };
    inline constexpr int kCatCount = static_cast<int>(Cat::Count);
    inline constexpr const char* kCatNames[kCatCount] = {"say", "shout", "yell", "tell", "party", "ls1", "ls2", "emote", "unity", "assist",
        "battle", "npc", "system", "error"};
    inline constexpr const char* kCatTitles[kCatCount] = {"Say", "Shout", "Yell", "Tell", "Party", "Linkshell 1", "Linkshell 2", "Emote",
        "Unity", "Assist", "Battle", "NPC", "System", "Error"};

    auto Categorize(int32_t mode) -> Cat;
    auto CatFromName(const std::string& name, Cat& out) -> bool;

    // Segment color: 0 = channel color, 1 = palette-1 highlight, 2 = palette-2 highlight, 3 = auto-translate.
    struct Segment
    {
        uint8_t color{0};
        uint8_t code{0};  // the escape's nn byte (kept for future exact palettes)
        std::string text; // UTF-8
    };

    // Decodes one raw line (may contain several physical lines separated by \n / \r).
    auto Decode(IChatManager* chat, const char* raw) -> std::vector<std::vector<Segment>>;
    auto PlainText(const std::vector<Segment>& segs) -> std::string;
} // namespace farplaneui::chat

#endif
