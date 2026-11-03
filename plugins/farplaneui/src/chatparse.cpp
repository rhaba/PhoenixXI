/**
 * Farplane UI - FFXI chat line decoding (see chatparse.hpp).
 */

#include "chatparse.hpp"

#include <algorithm>
#include <cstring>

namespace farplaneui::chat
{
    auto Categorize(int32_t rawMode) -> Cat
    {
        const int m = static_cast<int>(static_cast<uint32_t>(rawMode) & 0xFFu);
        switch (m)
        {
            case 1: case 9: return Cat::Say;
            case 2: case 10: return Cat::Shout;
            case 3: case 11: return Cat::Yell;
            case 4: case 12: return Cat::Tell;
            case 5: case 13: return Cat::Party;
            case 6: case 14: case 205: return Cat::Linkshell1;
            case 213: case 214: case 217: return Cat::Linkshell2;
            case 7: case 15: return Cat::Emote;
            case 211: case 212: return Cat::Unity;
            case 220: case 222: return Cat::Assist;
            case 142: case 144: case 150: case 151: case 152: case 210: return Cat::Npc;
            case 123: case 157: return Cat::Error;
            default: break;
        }
        if ((m >= 20 && m <= 69) || m == 80 || (m >= 100 && m <= 122 && m != 121) || m == 129 || m == 131 || (m >= 162 && m <= 189) || m == 191)
            return Cat::Battle;
        return Cat::System;
    }

    auto CatFromName(const std::string& name, Cat& out) -> bool
    {
        for (int i = 0; i < kCatCount; ++i)
        {
            if (_stricmp(name.c_str(), kCatNames[i]) == 0)
            {
                out = static_cast<Cat>(i);
                return true;
            }
        }
        return false;
    }

    namespace
    {
        auto IsSjisLead(uint8_t b) -> bool
        {
            return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
        }

        void AppendCp932(std::string& out, const char* bytes, int len)
        {
            wchar_t w[4]{};
            const int n = ::MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, len, w, 4);
            if (n <= 0)
                return; // FFXI-private glyph with no Unicode mapping: drop
            char u[16]{};
            const int k = ::WideCharToMultiByte(CP_UTF8, 0, w, n, u, static_cast<int>(sizeof(u)), nullptr, nullptr);
            if (k > 0)
            {
                // Ideographic space -> ASCII space keeps wrapping sane in Latin fonts.
                if (n == 1 && w[0] == 0x3000)
                    out += ' ';
                else
                    out.append(u, static_cast<size_t>(k));
            }
        }

        constexpr const char* kElements[] = {"[Fire]", "[Ice]", "[Wind]", "[Earth]", "[Lightning]", "[Water]", "[Light]", "[Dark]"};
    } // namespace

    auto Decode(IChatManager* chatMgr, const char* raw) -> std::vector<std::vector<Segment>>
    {
        std::vector<std::vector<Segment>> lines(1);
        if (raw == nullptr)
            return lines;

        // Expand raw 0xFD auto-translate tokens to bracketed phrases first (Ashita API).
        std::string src(raw);
        if (chatMgr != nullptr && src.find('\xFD') != std::string::npos)
        {
            std::vector<char> buf(8192, 0);
            if (chatMgr->ParseAutoTranslate(raw, buf.data(), static_cast<int32_t>(buf.size()), true) > 0)
                src.assign(buf.data());
        }

        uint8_t color = 0, code = 0;
        auto seg = [&](void) -> Segment& {
            auto& line = lines.back();
            if (line.empty() || line.back().color != color || line.back().code != code)
                line.push_back(Segment{color, code, {}});
            return line.back();
        };

        const auto* p   = reinterpret_cast<const uint8_t*>(src.data());
        const size_t n  = src.size();
        for (size_t i = 0; i < n; ++i)
        {
            const uint8_t b = p[i];
            if (b == 0x1E || b == 0x1F)
            {
                if (i + 1 < n)
                {
                    const uint8_t c = p[++i];
                    if (c == 0x01)
                    {
                        color = 0;
                        code  = 0;
                    }
                    else
                    {
                        color = (b == 0x1E) ? 1 : 2;
                        code  = c;
                    }
                }
                continue;
            }
            if (b == 0x7F)
            {
                ++i; // control pair
                continue;
            }
            if (b == '\n' || b == '\r')
            {
                if (!lines.back().empty())
                    lines.emplace_back();
                continue;
            }
            if (b == 0x07 || b == '\t')
            {
                seg().text += ' ';
                continue;
            }
            if (b < 0x20)
                continue;
            if (b < 0x80)
            {
                seg().text += static_cast<char>(b);
                continue;
            }
            if (b == 0xFD)
            {
                // Unresolved auto-translate token: FD xx xx xx xx FD
                size_t j = i + 1;
                while (j < n && p[j] != 0xFD && j - i < 8)
                    ++j;
                i = j;
                seg().text += "{?}";
                continue;
            }
            if (b == 0xEF && i + 1 < n)
            {
                const uint8_t t = p[i + 1];
                if (t == 0x27 || t == 0x28)
                {
                    ++i;
                    color = 3;
                    code  = 0;
                    seg().text += (t == 0x27) ? "{" : "}";
                    // Text inside the brackets keeps the auto-translate color until the close bracket.
                    if (t == 0x28)
                    {
                        color = 0;
                        code  = 0;
                    }
                    continue;
                }
                if (t >= 0x1F && t <= 0x26)
                {
                    ++i;
                    seg().text += kElements[t - 0x1F];
                    continue;
                }
            }
            if (IsSjisLead(b) && i + 1 < n)
            {
                const char pair[2] = {static_cast<char>(b), static_cast<char>(p[i + 1])};
                AppendCp932(seg().text, pair, 2);
                ++i;
                continue;
            }
            if (b >= 0xA1 && b <= 0xDF)
            {
                const char one[1] = {static_cast<char>(b)};
                AppendCp932(seg().text, one, 1); // half-width katakana
                continue;
            }
            // other stray high bytes: drop
        }
        while (lines.size() > 1 && lines.back().empty())
            lines.pop_back();
        return lines;
    }

    auto PlainText(const std::vector<Segment>& segs) -> std::string
    {
        std::string s;
        for (const auto& g : segs)
            s += g.text;
        return s;
    }
} // namespace farplaneui::chat
