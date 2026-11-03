/**
 * Farplane UI - theme palettes (port of phxui.lua).
 *
 * Colors are applied per window with PushStyleColor/PushStyleVar and popped after End(), so other
 * addons and plugins keep their own ImGui style.
 */

#ifndef FARPLANEUI_THEME_HPP_INCLUDED
#define FARPLANEUI_THEME_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"

#include <cstdint>
#include <cstring>

namespace farplaneui
{
    struct Rgba
    {
        float r, g, b, a;
    };

    struct Palette
    {
        const char* name;
        Rgba abyss, surface1, surface2, border, subtle;
        Rgba text, secondary, peach, muted, faint;
        Rgba royal, tint, hover, ember;
        Rgba danger, gold, success;
        bool squared;

        // Meaning-based names (phxui: ok/warn/bad/info/dim/accent).
        auto ok(void) const -> const Rgba& { return success; }
        auto warn(void) const -> const Rgba& { return ember; }
        auto bad(void) const -> const Rgba& { return danger; }
        auto info(void) const -> const Rgba& { return peach; }
        auto dim(void) const -> const Rgba& { return muted; }
        auto accent(void) const -> const Rgba& { return royal; }
    };

    constexpr auto hex(uint32_t rgb, float a = 1.0f) -> Rgba
    {
        return Rgba{
            static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
            static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<float>(rgb & 0xFF) / 255.0f,
            a};
    }

    // Same palettes and order as phxui.lua (ui.THEMES).
    inline constexpr Palette kThemes[] = {
        {"Phoenix", hex(0x180e0e), hex(0x291c1c), hex(0x321f1f), hex(0xd2abab, 0.20f), hex(0xd2abab, 0.12f),
         hex(0xfff8f8), hex(0xeae1e1), hex(0xd2abab), hex(0x8a6b6b), hex(0x5a4545),
         hex(0xc55151), hex(0xc55151, 0.30f), hex(0xd45e5e), hex(0xff8d79),
         hex(0xe04040), hex(0xc5a131), hex(0x63ba8a), false},
        {"Farplane", hex(0x141317), hex(0x1f2024), hex(0x2b2e34), hex(0xcfdcd8, 0.22f), hex(0xcfdcd8, 0.12f),
         hex(0xfff4e8), hex(0xeadfd2), hex(0xa8d8f0), hex(0x8d7a72), hex(0x574a47),
         hex(0xd2642a), hex(0xf08a3c, 0.28f), hex(0xe8783a), hex(0xffb347),
         hex(0xe5482f), hex(0xf2c45a), hex(0x7cc9b6), false},
        {"Farplane9", hex(0x141317), hex(0x1f2024), hex(0x2b2e34), hex(0xcfdcd8, 0.34f), hex(0xcfdcd8, 0.16f),
         hex(0xfff4e8), hex(0xeadfd2), hex(0xa8d8f0), hex(0x8d8580), hex(0x57514f),
         hex(0xd2642a), hex(0xf08a3c, 0.28f), hex(0xe8783a), hex(0xffb347),
         hex(0xff5a3a), hex(0xffd27a), hex(0x8fd6a0), true},
        {"Umbrella", hex(0x0b120d), hex(0x132018), hex(0x1a2a20), hex(0x9fd7a8, 0.20f), hex(0x9fd7a8, 0.12f),
         hex(0xf2fff4), hex(0xdcebdf), hex(0xa8d9b0), hex(0x688a70), hex(0x43584a),
         hex(0x3f9e58), hex(0x3f9e58, 0.30f), hex(0x4fb86a), hex(0xf2c14e),
         hex(0xe04040), hex(0xd4b23c), hex(0x6fd98c), false},
        {"Midnight", hex(0x0e1218), hex(0x182030), hex(0x1f2a3b), hex(0xabc0d2, 0.20f), hex(0xabc0d2, 0.12f),
         hex(0xf6f9ff), hex(0xdfe6ef), hex(0xa9c3de), hex(0x6a7a90), hex(0x475365),
         hex(0x4f7fd1), hex(0x4f7fd1, 0.30f), hex(0x5f90e2), hex(0xffa36b),
         hex(0xe05050), hex(0xd0ac3c), hex(0x5fc38f), false},
        {"Classic", hex(0x0f0f0f), hex(0x1f1f1f), hex(0x2a2a2a), hex(0xffffff, 0.15f), hex(0xffffff, 0.08f),
         hex(0xffffff), hex(0xe6e6e6), hex(0xb4c8e6), hex(0x8c8c8c), hex(0x5c5c5c),
         hex(0x4296fa), hex(0x4296fa, 0.30f), hex(0x5aa5ff), hex(0xffa54f),
         hex(0xff5a5a), hex(0xe6c84f), hex(0x6ccf7f), false},
    };
    inline constexpr int kThemeCount = static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0]));
    inline constexpr int kDefaultTheme = 2; // Farplane9 (phxui PACK_THEME)

    inline auto FindTheme(const char* name) -> int
    {
        if (name == nullptr)
            return -1;
        for (int i = 0; i < kThemeCount; ++i)
        {
            if (_stricmp(kThemes[i].name, name) == 0)
                return i;
        }
        return -1;
    }

    inline auto ToVec4(const Rgba& c, float alphaMul = 1.0f) -> ImVec4
    {
        return ImVec4(c.r, c.g, c.b, c.a * alphaMul);
    }

    /**
     * Scoped theme: pushes the phxui style colors/vars in the constructor and pops exactly what it
     * pushed in the destructor. Construct before Begin(), let it fall out of scope after End().
     */
    class ScopedTheme
    {
        IGuiManager* gui_;
        int colors_{0};
        int vars_{0};

        void Col(ImGuiCol idx, const Rgba& c)
        {
            gui_->PushStyleColor(idx, ToVec4(c));
            ++colors_;
        }
        void Var(ImGuiStyleVar idx, float v)
        {
            gui_->PushStyleVar(idx, v);
            ++vars_;
        }
        void Var(ImGuiStyleVar idx, const ImVec2& v)
        {
            gui_->PushStyleVar(idx, v);
            ++vars_;
        }

    public:
        ScopedTheme(IGuiManager* gui, const Palette& c, float windowAlpha = 0.94f)
            : gui_{gui}
        {
            const Rgba clear{0, 0, 0, 0};
            Col(ImGuiCol_WindowBg, Rgba{c.abyss.r, c.abyss.g, c.abyss.b, windowAlpha});
            Col(ImGuiCol_ChildBg, clear);
            Col(ImGuiCol_TitleBg, c.surface1);
            Col(ImGuiCol_TitleBgActive, c.surface2);
            Col(ImGuiCol_TitleBgCollapsed, c.surface1);
            Col(ImGuiCol_MenuBarBg, c.surface1);
            Col(ImGuiCol_Border, c.border);
            Col(ImGuiCol_Separator, c.border);
            Col(ImGuiCol_SeparatorHovered, c.hover);
            Col(ImGuiCol_SeparatorActive, c.royal);
            Col(ImGuiCol_Text, c.text);
            Col(ImGuiCol_TextDisabled, c.muted);
            Col(ImGuiCol_FrameBg, c.surface2);
            Col(ImGuiCol_FrameBgHovered, c.tint);
            Col(ImGuiCol_FrameBgActive, c.surface2);
            Col(ImGuiCol_SliderGrab, c.royal);
            Col(ImGuiCol_SliderGrabActive, c.hover);
            Col(ImGuiCol_PlotHistogram, c.royal);
            Col(ImGuiCol_Header, c.surface2);
            Col(ImGuiCol_HeaderHovered, c.tint);
            Col(ImGuiCol_HeaderActive, c.royal);
            Col(ImGuiCol_Tab, c.surface1);
            Col(ImGuiCol_TabHovered, c.hover);
            Col(ImGuiCol_TabSelected, c.royal);
            Col(ImGuiCol_PopupBg, c.surface1);
            Col(ImGuiCol_CheckMark, c.royal);
            Col(ImGuiCol_Button, c.royal);
            Col(ImGuiCol_ButtonHovered, c.hover);
            Col(ImGuiCol_ButtonActive, c.royal);
            Col(ImGuiCol_ScrollbarBg, c.abyss);
            Col(ImGuiCol_ScrollbarGrab, c.surface2);
            Col(ImGuiCol_ScrollbarGrabHovered, c.tint);
            Col(ImGuiCol_ScrollbarGrabActive, c.royal);
            Col(ImGuiCol_ResizeGrip, clear);
            Col(ImGuiCol_ResizeGripHovered, c.tint);
            Col(ImGuiCol_ResizeGripActive, c.royal);

            const float round = c.squared ? 0.0f : 4.0f;
            Var(ImGuiStyleVar_WindowRounding, c.squared ? 0.0f : 8.0f);
            Var(ImGuiStyleVar_ChildRounding, c.squared ? 0.0f : 6.0f);
            Var(ImGuiStyleVar_FrameRounding, round);
            Var(ImGuiStyleVar_PopupRounding, c.squared ? 0.0f : 6.0f);
            Var(ImGuiStyleVar_GrabRounding, round);
            Var(ImGuiStyleVar_TabRounding, round);
            if (c.squared)
            {
                Var(ImGuiStyleVar_ScrollbarRounding, 0.0f);
                Var(ImGuiStyleVar_WindowBorderSize, 1.0f);
                Var(ImGuiStyleVar_FrameBorderSize, 1.0f);
                Var(ImGuiStyleVar_PopupBorderSize, 1.0f);
            }
            Var(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            Var(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
            Var(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
        }

        ~ScopedTheme(void)
        {
            gui_->PopStyleColor(colors_);
            gui_->PopStyleVar(vars_);
        }

        ScopedTheme(const ScopedTheme&)            = delete;
        ScopedTheme& operator=(const ScopedTheme&) = delete;
    };

} // namespace farplaneui

#endif // FARPLANEUI_THEME_HPP_INCLUDED
