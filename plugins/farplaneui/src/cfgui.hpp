/**
 * Farplane UI - configuration window widgets.
 *
 * Settings are laid out as cards in a two-column grid (Ember Glass): a label, the control (toggles and
 * buttons on the label's row, sliders and pickers full width under it) and the description wrapped
 * in full underneath.
 *
 *   cfg::Grid g(gui, ctx);
 *   ch |= g.Toggle("Enabled", "Show the minimap.", enabled_);
 *   g.Section("Size");
 *   ch |= g.Slider("Zoom", "Map magnification.", zoom_, 0.25f, 8.0f, "%.2f");
 *   g.Free();   // full-width free-form ImGui until the next card / Section
 */

#ifndef FARPLANEUI_CFGUI_HPP_INCLUDED
#define FARPLANEUI_CFGUI_HPP_INCLUDED

#include "module.hpp"

namespace farplaneui::cfg
{
    class Grid
    {
    public:
        Grid(IGuiManager* gui, Context& ctx);
        ~Grid(void);
        Grid(const Grid&)            = delete;
        Grid& operator=(const Grid&) = delete;

        // Section title spanning both columns; starts a new grid underneath.
        void Section(const char* title);
        // Ends the grid so the caller can draw full-width ImGui; the next card starts a new grid.
        void Free(void);

        auto Toggle(const char* label, const char* desc, bool& v) -> bool;
        auto Slider(const char* label, const char* desc, float& v, float min, float max, const char* fmt = "%.2f") -> bool;
        auto Slider(const char* label, const char* desc, int& v, int min, int max, const char* fmt = "%d") -> bool;
        auto Choice(const char* label, const char* desc, int& index, const char* const* items, int count) -> bool;
        auto Button(const char* label, const char* desc, const char* button) -> bool;
        // Read-only value (status lines).
        void Info(const char* label, const char* value);

        auto Gui(void) const -> IGuiManager* { return gui_; }

    private:
        void Open(void);
        void Close(void);
        // Starts a card: label + description; leaves the cursor where a control of `controlW` goes.
        void Card(const char* label, const char* desc, float controlW);
        void EndCard(const char* desc);

        IGuiManager* gui_;
        Context& ctx_;
        bool open_{false};
        int grids_{0};
        ImVec2 cardStart_{};
        float cardW_{0}, labelBottom_{0};
        int cell_{0}; // cells in the current table (a row line goes above each row after the first)
    };
} // namespace farplaneui::cfg

#endif // FARPLANEUI_CFGUI_HPP_INCLUDED
