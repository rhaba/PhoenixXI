/**
 * Farplane UI - settings (same keys and defaults as the Lua addon's settings table).
 *
 * Stored with Ashita's IConfigurationManager in <Ashita>/config/farplaneui/farplaneui.ini,
 * section [settings].
 */

#ifndef FARPLANEUI_SETTINGS_HPP_INCLUDED
#define FARPLANEUI_SETTINGS_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"

#include <string>

namespace farplaneui
{
    struct Settings
    {
        bool enabled{true};
        std::string theme{"Farplane9"};
        int lines{10};
        int lineHeight{20};
        int padding{36};
        int fade{30};
        float opacity{0.88f};
        bool accentLine{true};
        bool pyreflies{true};
        std::string pyreflyStyle{"sending"}; // "sending" or "motes"
        int pyreflyCount{60};
        bool hideInCutscenes{true};
        bool followFancyChat{false}; // kept for compatibility; FancyChat anchor is not ported yet
        int chatMargin{-47};
        int bottomGap{0};
        bool bottomGapAuto{false};
        std::string maskLayer{"under_ui"}; // "under_ui" or "over_ui"
        std::string maskImage{"auto"};     // band background: "off" (classic fade), "auto" (closest width) or a file in the artkit folder
        float maskImageOpacity{1.0f};
        int settingsRev{5};

        // C++-only
        bool showParty{true};

        auto Sending(void) const -> bool { return pyreflyStyle != "motes"; }
        auto UnderUi(void) const -> bool { return maskLayer != "over_ui"; }

        void Load(IAshitaCore* core);
        void Save(IAshitaCore* core) const;
        void Clamp(void);
    };

} // namespace farplaneui

#endif // FARPLANEUI_SETTINGS_HPP_INCLUDED
