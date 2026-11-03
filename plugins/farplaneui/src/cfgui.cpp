/**
 * Farplane UI - configuration window widgets (see cfgui.hpp), Ember Glass style
 * (incoming/ember_wiring2 WIRING.md C2-C5):
 *   - two-column table, a faint amber line between rows; each cell = label (ivory), the control,
 *     then the description wrapped in full (never cut off);
 *   - toggles: ON/OFF label + an animated pill switch, right-aligned on the label line;
 *   - sliders: [-] [track with ember fill + amber grab] [+]; - and + repeat while held, Shift = x10,
 *     Ctrl+click the slider to type a value; values snap to the step grid;
 *   - choices: [<] [ name (i/n) ] [>]; arrows repeat and wrap, the middle opens a list of all options.
 */

#include "cfgui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <type_traits>
#include <string>
#include <unordered_map>

namespace farplaneui::cfg
{
    namespace
    {
        constexpr Rgba kIvory{0xf3 / 255.0f, 0xe9 / 255.0f, 0xd8 / 255.0f, 1}, kAmber{0xe6 / 255.0f, 0xb4 / 255.0f, 0x79 / 255.0f, 1};
        constexpr Rgba kEmber{0xcf / 255.0f, 0x73 / 255.0f, 0x3e / 255.0f, 1}, kDim{0x9a / 255.0f, 0x8f / 255.0f, 0x80 / 255.0f, 1};
        constexpr Rgba kTrack{0x0a / 255.0f, 0x0c / 255.0f, 0x18 / 255.0f, 0xd2 / 255.0f}, kAmberDim{0xe6 / 255.0f, 0xb4 / 255.0f, 0x79 / 255.0f, 0x3c / 255.0f};
        constexpr Rgba kRowLine{0xe6 / 255.0f, 0xb4 / 255.0f, 0x79 / 255.0f, 28.0f / 255.0f};
        constexpr float kPillW = 46.0f, kPillH = 18.0f;

        // Rounded rectangle with an optional border (an empty ImGui frame).
        void Round(IGuiManager* gui, const ImVec2& p, const ImVec2& sz, const Rgba& fill, const Rgba& edge, float r, float border)
        {
            if (sz.x < 1 || sz.y < 1)
                return;
            const ImVec2 keep = gui->GetCursorScreenPos();
            gui->PushStyleVar(ImGuiStyleVar_FrameRounding, r);
            gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, border);
            gui->PushStyleColor(ImGuiCol_FrameBg, ToVec4(fill));
            gui->PushStyleColor(ImGuiCol_Border, ToVec4(edge));
            gui->SetCursorScreenPos(p);
            gui->ProgressBar(0.0f, sz, "##");
            gui->PopStyleColor(2);
            gui->PopStyleVar(2);
            gui->SetCursorScreenPos(keep);
        }
        // Small round button (-, +, <, >) that repeats while held.
        auto RoundButton(IGuiManager* gui, const char* id, const char* glyph, float size) -> bool
        {
            gui->PushStyleVar(ImGuiStyleVar_FrameRounding, size * 0.5f);
            gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            gui->PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            gui->PushStyleColor(ImGuiCol_Button, ToVec4(kTrack));
            gui->PushStyleColor(ImGuiCol_ButtonHovered, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.33f}));
            gui->PushStyleColor(ImGuiCol_ButtonActive, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.66f}));
            gui->PushStyleColor(ImGuiCol_Border, ToVec4(kAmber));
            gui->PushStyleColor(ImGuiCol_Text, ToVec4(kAmber));
            gui->PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            const bool hit = gui->Button((std::string(glyph) + id).c_str(), ImVec2(size, size));
            gui->PopItemFlag();
            gui->PopStyleColor(5);
            gui->PopStyleVar(3);
            return hit;
        }
        // Step from the display format: "%.2f" -> 0.01, "%.0f" -> 1.
        auto StepFor(const char* fmt) -> float
        {
            const char* dot = fmt != nullptr ? std::strchr(fmt, '.') : nullptr;
            const int decimals = dot != nullptr && std::isdigit(static_cast<unsigned char>(dot[1])) ? dot[1] - '0' : 2;
            return std::pow(10.0f, -static_cast<float>(decimals));
        }
        // Knob positions of the pill toggles (animated).
        auto Knobs(void) -> std::unordered_map<std::string, float>&
        {
            static std::unordered_map<std::string, float> knobs;
            return knobs;
        }
    } // namespace

    Grid::Grid(IGuiManager* gui, Context& ctx) : gui_{gui}, ctx_{ctx} {}
    Grid::~Grid(void) { Close(); }

    void Grid::Open(void)
    {
        if (open_)
            return;
        char id[32]{};
        ::sprintf_s(id, "##cfg_grid_%d", grids_++);
        gui_->PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 8.0f));
        open_ = gui_->BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX);
        gui_->PopStyleVar();
        cell_ = 0;
    }
    void Grid::Close(void)
    {
        if (open_)
            gui_->EndTable();
        open_ = false;
    }
    void Grid::Free(void) { Close(); }

    void Grid::Section(const char* title)
    {
        Close();
        gui_->Dummy(ImVec2(0, 4));
        gui_->TextColored(ToVec4(kAmber), "%s", title);
        const ImVec2 p = gui_->GetCursorScreenPos();
        ui::ImageAt(gui_, ctx_.white, p, ImVec2(gui_->GetContentRegionAvail().x, 1), ImVec2(0, 0), ImVec2(1, 1), kAmber, 0.18f);
        gui_->Dummy(ImVec2(0, 4));
    }

    // Starts a cell: a faint line above it (rows after the first), the label, and `controlW` px kept
    // free on the label's right for an inline control (toggle / button). Returns the label line's y.
    void Grid::Card(const char* label, const char* desc, float controlW)
    {
        (void)desc;
        Open();
        gui_->TableNextColumn();
        cardStart_ = gui_->GetCursorScreenPos();
        cardW_     = gui_->GetContentRegionAvail().x;
        if (cell_ >= 2 && ctx_.white != nullptr)
            ui::ImageAt(gui_, ctx_.white, ImVec2(cardStart_.x, cardStart_.y - 5.0f), ImVec2(cardW_, 1), ImVec2(0, 0), ImVec2(1, 1), kRowLine);
        ++cell_;
        const float lineH = std::max(gui_->GetFrameHeight(), kPillH + 4.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, cardStart_.y + (lineH - gui_->GetTextLineHeight()) * 0.5f));
        gui_->PushTextWrapPos(gui_->GetCursorPosX() + std::max(40.0f, cardW_ - controlW - 10.0f));
        gui_->TextColored(ToVec4(kIvory), "%s", label);
        gui_->PopTextWrapPos();
        labelBottom_ = std::max(gui_->GetCursorScreenPos().y, cardStart_.y + lineH);
        gui_->PushID(label);
    }
    // Description under the control, wrapped to the cell (the whole text, no ellipsis).
    void Grid::EndCard(const char* desc)
    {
        gui_->PopID();
        if (desc != nullptr && desc[0] != '\0')
        {
            gui_->SetCursorScreenPos(ImVec2(cardStart_.x, gui_->GetCursorScreenPos().y + 1.0f));
            gui_->PushTextWrapPos(gui_->GetCursorPosX() + cardW_);
            gui_->TextColored(ToVec4(kDim), "%s", desc);
            gui_->PopTextWrapPos();
        }
        gui_->Dummy(ImVec2(cardW_, 2.0f));
    }

    auto Grid::Toggle(const char* label, const char* desc, bool& v) -> bool
    {
        const float onW = gui_->CalcTextSize("OFF").x + 8.0f;
        Card(label, desc, kPillW + onW);
        const ImVec2 p(cardStart_.x + cardW_ - kPillW, cardStart_.y + 2.0f);
        gui_->SetCursorScreenPos(ImVec2(p.x - onW, cardStart_.y + (kPillH + 4.0f - gui_->GetTextLineHeight()) * 0.5f));
        gui_->TextColored(ToVec4(v ? kAmber : kDim), v ? "ON" : "OFF");
        gui_->SetCursorScreenPos(p);
        bool changed = false;
        if (gui_->InvisibleButton("##pill", ImVec2(kPillW, kPillH)))
            v = !v, changed = true;
        // Animated knob.
        float& k = Knobs()[std::to_string(reinterpret_cast<uintptr_t>(&v))];
        k += ((v ? 1.0f : 0.0f) - k) * std::min(1.0f, gui_->GetIO().DeltaTime * 18.0f);
        Round(gui_, p, ImVec2(kPillW, kPillH), v ? kEmber : kTrack, v ? kAmber : kAmberDim, kPillH * 0.5f, 1.0f);
        const float kx = p.x + 3.0f + k * (kPillW - kPillH);
        Round(gui_, ImVec2(kx, p.y + 2.0f), ImVec2(kPillH - 4.0f, kPillH - 4.0f), v ? kIvory : kDim, Rgba{0, 0, 0, 0}, (kPillH - 4.0f) * 0.5f, 0.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_));
        EndCard(desc);
        return changed;
    }

    // [-] [====|====] [+] across the cell, under the label.
    template <typename T>
    static auto Stepper(IGuiManager* gui, T& v, T lo, T hi, T step, const char* fmt, float width) -> bool
    {
        const float h = gui->GetFrameHeight();
        const T s     = gui->GetIO().KeyShift ? step * 10 : step;
        auto snap = [&](T x) -> T {
            x = std::clamp(x, lo, hi);
            if constexpr (std::is_floating_point_v<T>)
                x = std::clamp(static_cast<T>(lo + std::round((x - lo) / step) * step), lo, hi);
            return x;
        };
        bool changed = false;
        if (RoundButton(gui, "##dec", "-", h))
            v = snap(v - s), changed = true;
        gui->SameLine(0.0f, 6.0f);
        const ImVec2 tp = gui->GetCursorScreenPos();
        const float tw  = std::max(40.0f, width - 2 * (h + 6.0f));
        // Track + ember fill drawn under a transparent slider frame; the amber grab is the slider's.
        const float frac = hi > lo ? static_cast<float>(v - lo) / static_cast<float>(hi - lo) : 0.0f;
        Round(gui, tp, ImVec2(tw, h), kTrack, kAmberDim, h * 0.5f, 1.0f);
        if (frac > 0.0f)
            Round(gui, ImVec2(tp.x + 1, tp.y + 1), ImVec2(std::max(h - 2, (tw - 2) * std::clamp(frac, 0.0f, 1.0f)), h - 2), Rgba{kEmber.r, kEmber.g, kEmber.b, 0.82f},
                Rgba{0, 0, 0, 0}, (h - 2) * 0.5f, 0.0f);
        gui->PushStyleColor(ImGuiCol_FrameBg, ToVec4(Rgba{0, 0, 0, 0}));
        gui->PushStyleColor(ImGuiCol_FrameBgHovered, ToVec4(Rgba{1, 1, 1, 0.04f}));
        gui->PushStyleColor(ImGuiCol_FrameBgActive, ToVec4(Rgba{1, 1, 1, 0.06f}));
        gui->PushStyleColor(ImGuiCol_SliderGrab, ToVec4(kAmber));
        gui->PushStyleColor(ImGuiCol_SliderGrabActive, ToVec4(kIvory));
        gui->PushStyleColor(ImGuiCol_Text, ToVec4(kIvory));
        gui->PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
        gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        gui->PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.0f);
        gui->PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f);
        gui->SetNextItemWidth(tw);
        if constexpr (std::is_integral_v<T>)
            changed |= gui->SliderInt("##s", &v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
        else
        {
            if (gui->SliderFloat("##s", &v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp))
                v = snap(v), changed = true;
        }
        gui->PopStyleVar(4);
        gui->PopStyleColor(6);
        gui->SameLine(0.0f, 6.0f);
        if (RoundButton(gui, "##inc", "+", h))
            v = snap(v + s), changed = true;
        return changed;
    }

    auto Grid::Slider(const char* label, const char* desc, float& v, float min, float max, const char* fmt) -> bool
    {
        Card(label, desc, 0.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_ + 2.0f));
        const bool changed = Stepper<float>(gui_, v, min, max, StepFor(fmt), fmt, cardW_);
        EndCard(desc);
        return changed;
    }
    auto Grid::Slider(const char* label, const char* desc, int& v, int min, int max, const char* fmt) -> bool
    {
        Card(label, desc, 0.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_ + 2.0f));
        const bool changed = Stepper<int>(gui_, v, min, max, 1, fmt, cardW_);
        EndCard(desc);
        return changed;
    }

    // [<] [ name (i/n) ] [>]: the arrows repeat and wrap; the middle opens a list of every option.
    auto Grid::Choice(const char* label, const char* desc, int& index, const char* const* items, int count) -> bool
    {
        Card(label, desc, 0.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_ + 2.0f));
        bool changed = false;
        if (count > 0)
        {
            index = std::clamp(index, 0, count - 1);
            const float h = gui_->GetFrameHeight();
            if (RoundButton(gui_, "##l", "<", h))
                index = (index + count - 1) % count, changed = true;
            gui_->SameLine(0.0f, 6.0f);
            const float mw = std::max(40.0f, cardW_ - 2 * (h + 6.0f));
            char buf[160]{};
            ::sprintf_s(buf, "%s (%d/%d)", items[index], index + 1, count);
            std::string text = buf;
            if (gui_->CalcTextSize(text.c_str()).x > mw - 20.0f)
            {
                while (!text.empty() && gui_->CalcTextSize((text + "...").c_str()).x > mw - 20.0f)
                    text.pop_back();
                text += "...";
            }
            gui_->PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
            gui_->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            gui_->PushStyleColor(ImGuiCol_Button, ToVec4(kTrack));
            gui_->PushStyleColor(ImGuiCol_ButtonHovered, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.25f}));
            gui_->PushStyleColor(ImGuiCol_ButtonActive, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.45f}));
            gui_->PushStyleColor(ImGuiCol_Border, ToVec4(kAmberDim));
            gui_->PushStyleColor(ImGuiCol_Text, ToVec4(kIvory));
            if (gui_->Button((text + "##v").c_str(), ImVec2(mw, h)))
                gui_->OpenPopup("##list");
            if (text != buf && gui_->IsItemHovered())
                gui_->SetTooltip("%s", buf);
            gui_->PopStyleColor(5);
            gui_->PopStyleVar(2);
            if (gui_->BeginPopup("##list"))
            {
                for (int i = 0; i < count; ++i)
                    if (gui_->Selectable(items[i], i == index))
                        index = i, changed = true;
                gui_->EndPopup();
            }
            gui_->SameLine(0.0f, 6.0f);
            if (RoundButton(gui_, "##r", ">", h))
                index = (index + 1) % count, changed = true;
        }
        EndCard(desc);
        return changed;
    }

    auto Grid::Button(const char* label, const char* desc, const char* button) -> bool
    {
        const float w = std::max(kPillW, gui_->CalcTextSize(button).x + 24.0f);
        Card(label, desc, w);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x + cardW_ - w, cardStart_.y));
        gui_->PushStyleVar(ImGuiStyleVar_FrameRounding, gui_->GetFrameHeight() * 0.5f);
        gui_->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        gui_->PushStyleColor(ImGuiCol_Button, ToVec4(kTrack));
        gui_->PushStyleColor(ImGuiCol_ButtonHovered, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.33f}));
        gui_->PushStyleColor(ImGuiCol_ButtonActive, ToVec4(Rgba{kEmber.r, kEmber.g, kEmber.b, 0.66f}));
        gui_->PushStyleColor(ImGuiCol_Border, ToVec4(kAmber));
        gui_->PushStyleColor(ImGuiCol_Text, ToVec4(kAmber));
        const bool clicked = gui_->Button(button, ImVec2(w, 0));
        gui_->PopStyleColor(5);
        gui_->PopStyleVar(2);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_));
        EndCard(desc);
        return clicked;
    }

    void Grid::Info(const char* label, const char* value)
    {
        Card(label, nullptr, 0.0f);
        gui_->SetCursorScreenPos(ImVec2(cardStart_.x, labelBottom_));
        EndCard(value);
    }
} // namespace farplaneui::cfg
