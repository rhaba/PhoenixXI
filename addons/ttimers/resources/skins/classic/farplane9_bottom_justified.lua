--[[
Copyright (c) 2024 Thorny

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
--]]

-- Farplane IX: Final Fantasy IX gauges in Farplane colors (Spongeh's PhoenixXI addon pack).
-- Textures drawn by tools/make_farplane9_skin.py. The outline keeps its own colors (BG is white);
-- the bar is tinted green -> amber -> ember red as the timer runs low, like the farplane9 HP bars.
local settings = T{
    Bar = T{
        Texture = 'skins/classic/assets/bar_farplane9.png',
        OutlineTexture = 'skins/classic/assets/outline_farplane9.png',
        Width = 215,
        Height = 16,
        BaseOffsetX = 0,
        BaseOffsetY = -17,
        NameOffsetX = 20,
        NameOffsetY = 1.5,
        TimerOffsetX = 6,
        TimerOffsetY = 1.5,
        HorizontalSpacing = 0,
        VerticalSpacing = -17,
    };
    Color = T{
        Blend = true,
        LowThreshold = 10,
        MidThreshold = 30,
        HighThreshold = 60,
        BG = 0xFFFFFFFF,
        Low = 0xFFFF5A3A,
        Middle = 0xFFFFD27A,
        High = 0xFF8FD6A0,
    };
    Icon = T{
        OffsetX = 0,
        OffsetY = 0,
        Width = 16,
        Height = 16,
    };
    Label = T{
        font_color = 0xFFFFF4E8,
        font_family = 'Arial',
        support_jp = false,
        font_height = 11,
        outline_color = 0xFF141317,
        outline_width = 2,
        visible = true,
    };
    ToolTip = T{
        offset_x = 0,
        offset_y = 12,
        font_color = 0xFFFFF4E8,
        font_family = 'Consolas',
        support_jp = false,
        font_height = 10,
        visible = true,
        background = T{
            visible = true,
            corner_rounding = 4,
            outline_width = 2,
            fill_color = 0xF0141317,
        }
    };
    DragHandle = T{
        offset_x = 0,
        offset_y = -15,
        width = 40,
        height = 40,
        corner_rounding = 4,
        fill_color = 0xC0D2642A,
        outline_color = 0xFF141317,
        outline_width = 1,
        visible = true;
    };
};
return settings;