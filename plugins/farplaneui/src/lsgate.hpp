/**
 * Farplane UI - linkshell gate: the UI only runs for members wearing the Farplane linkshell.
 *
 * A linkshell item (resource type 6) keeps its name packed in the item's extra data from offset 0x09:
 * 6 bits per character, most significant bit first, 1-26 = a-z, 27-52 = A-Z, 0 = padding (the scheme
 * Ashita's libs/ffxi/itemdata.lua parse_linkshell_name uses). An equipped linkshell has item status 19.
 */

#ifndef FARPLANEUI_LSGATE_HPP_INCLUDED
#define FARPLANEUI_LSGATE_HPP_INCLUDED

#include "Ashita.h"

#include <string>
#include <vector>

namespace farplaneui::lsgate
{
    inline constexpr const char* kRequired = "Farplane";

    struct Shell
    {
        std::string name;
        uint32_t flags{0}; // item status (19 = equipped)
    };

    inline auto DecodeName(const uint8_t* extra, size_t size, size_t offset) -> std::string
    {
        static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::string out;
        const size_t bits = (size - offset) * 8;
        for (size_t b = 0; b + 6 <= bits; b += 6)
        {
            uint32_t v = 0;
            for (size_t i = 0; i < 6; ++i)
            {
                const size_t bit = b + i;
                v = (v << 1) | ((extra[offset + bit / 8] >> (7 - bit % 8)) & 1u);
            }
            if (v == 0)
                break;
            if (v <= 52)
                out += kChars[v - 1];
        }
        return out;
    }

    // Linkshell items in the inventory, with their names and status.
    inline auto Shells(IAshitaCore* core) -> std::vector<Shell>
    {
        std::vector<Shell> out;
        auto* mm  = core != nullptr ? core->GetMemoryManager() : nullptr;
        auto* inv = mm != nullptr ? mm->GetInventory() : nullptr;
        auto* res = core != nullptr ? core->GetResourceManager() : nullptr;
        if (inv == nullptr || res == nullptr)
            return out;
        const uint32_t n = std::min<uint32_t>(inv->GetContainerCountMax(0), 81);
        for (uint32_t i = 1; i < n; ++i)
        {
            const auto* it = inv->GetContainerItem(0, i);
            if (it == nullptr || it->Id == 0 || it->Id == 0xFFFF)
                continue;
            const IItem* r = res->GetItemById(it->Id);
            if (r == nullptr || r->Type != 6)
                continue;
            out.push_back({DecodeName(it->Extra, sizeof(it->Extra), 0x09), it->Flags});
        }
        return out;
    }

    // True when the Farplane linkshell is equipped.
    inline auto Member(IAshitaCore* core) -> bool
    {
        for (const auto& s : Shells(core))
            if (s.flags == 19 && s.name == kRequired)
                return true;
        return false;
    }
} // namespace farplaneui::lsgate

#endif // FARPLANEUI_LSGATE_HPP_INCLUDED
