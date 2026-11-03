/**
 * Farplane UI - controller input for FarplaneUI's own menus (implemented in modules/mod_pad.cpp).
 *
 * Read through Ashita's DirectInput controller hook (offset 32 = D-pad hat, 48 + n = button n). Each
 * action is a button number from [pad] in the ini (defaults from a DualShock-style pad on PhoenixXI);
 * the D-pad is the hat. While FarplaneUI holds the controller (SetCapture), those buttons, the hat and
 * the left stick are kept from the game.
 */

#ifndef FARPLANEUI_PAD_HPP_INCLUDED
#define FARPLANEUI_PAD_HPP_INCLUDED

namespace farplaneui::pad
{
    enum Action : int
    {
        Up,
        Down,
        Left,
        Right,
        Confirm,
        Cancel,
        PrevPage,    // L1: previous settings category
        NextPage,    // R1: next category
        PrevSection, // L2: previous sub-tab
        NextSection, // R2: next sub-tab
        Open,        // touchpad: open / close the FarplaneUI settings
        kActions
    };
    // Pressed this frame (the D-pad and left / right repeat while held).
    auto Pressed(Action a) -> bool;
    // Held for this long (seconds), 0 if up.
    auto HeldFor(Action a) -> float;
    // FarplaneUI owns the controller: its buttons, the D-pad and the left stick don't reach the game.
    void SetCapture(bool on);
    auto Captured(void) -> bool;
} // namespace farplaneui::pad

#endif // FARPLANEUI_PAD_HPP_INCLUDED
