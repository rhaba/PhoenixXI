/**
 * Farplane UI - shared UI helpers and the texture cache.
 */

#include "module.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace farplaneui
{
    // ---------------------------------------------------------------------------------------------
    // TextureCache
    // ---------------------------------------------------------------------------------------------
    void TextureCache::Release(void)
    {
        for (auto& [k, e] : files_)
        {
            if (e.tex != nullptr)
                e.tex->Release();
        }
        for (auto& [k, e] : mem_)
        {
            if (e.tex != nullptr)
                e.tex->Release();
        }
        files_.clear();
        mem_.clear();
        device_ = nullptr;
    }

    auto TextureCache::File(const std::string& path, UINT* w, UINT* h) -> IDirect3DTexture8*
    {
        auto& e = files_[path];
        if (!e.tried && device_ != nullptr)
        {
            e.tried = true;
            D3DXIMAGE_INFO info{};
            if (FAILED(::D3DXCreateTextureFromFileExA(device_, path.c_str(), D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                    D3DX_DEFAULT, D3DX_DEFAULT, 0, &info, nullptr, &e.tex)))
                e.tex = nullptr;
            e.w = info.Width;
            e.h = info.Height;
        }
        if (w != nullptr)
            *w = e.w;
        if (h != nullptr)
            *h = e.h;
        return e.tex;
    }

    auto TextureCache::Memory(uint32_t key, const void* data, uint32_t size) -> IDirect3DTexture8*
    {
        auto& e = mem_[key];
        if (!e.tried && device_ != nullptr && data != nullptr && size > 0)
        {
            e.tried = true;
            // Item icons are 32x32 BMPs; black (0xFF000000) is the transparent color key (equipmon).
            if (FAILED(::D3DXCreateTextureFromFileInMemoryEx(device_, data, size, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                    D3DX_DEFAULT, D3DX_DEFAULT, 0xFF000000, nullptr, nullptr, &e.tex)))
                e.tex = nullptr;
        }
        return e.tex;
    }

    auto TextureCache::LoadedFiles(void) const -> std::vector<std::pair<std::string, bool>>
    {
        std::vector<std::pair<std::string, bool>> out;
        for (const auto& [k, e] : files_)
            out.emplace_back(k, e.tex != nullptr);
        return out;
    }

    namespace ui
    {
        auto Lower(std::string s) -> std::string
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return s;
        }

        auto ParseInt(const std::string& s, int& out) -> bool
        {
            if (s.empty())
                return false;
            char* end    = nullptr;
            const long v = std::strtol(s.c_str(), &end, 10);
            if (end == nullptr || *end != '\0')
                return false;
            out = static_cast<int>(v);
            return true;
        }

        auto ParseFloat(const std::string& s, float& out) -> bool
        {
            if (s.empty())
                return false;
            char* end = nullptr;
            const float v = std::strtof(s.c_str(), &end);
            if (end == nullptr || *end != '\0')
                return false;
            out = v;
            return true;
        }

        auto Join(const std::vector<std::string>& v, size_t from, const char* sep) -> std::string
        {
            std::string out;
            for (size_t i = from; i < v.size(); ++i)
            {
                if (i > from)
                    out += sep;
                out += v[i];
            }
            return out;
        }

        auto Tex(IDirect3DTexture8* t) -> ImTextureRef
        {
            // Ashita's ImGui D3D8 backend takes the IDirect3DTexture8* as the texture id (as Lua addons do).
            return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(t)));
        }

        void Tooltip(IGuiManager* gui, const char* text)
        {
            if (gui->IsItemHovered())
                gui->SetTooltip("%s", text);
        }

        void Section(IGuiManager* gui, const Palette& c, const char* label)
        {
            gui->Spacing();
            gui->TextColored(ToVec4(c.accent()), "%s", label);
            gui->Separator();
        }

        void AccentLine(IGuiManager* gui, const Context& ctx, const Rgba& color)
        {
            const float w = std::max(40.0f, gui->GetContentRegionAvail().x);
            if (ctx.hfade != nullptr)
            {
                gui->ImageWithBg(Tex(ctx.hfade), ImVec2(w, 2), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ToVec4(color));
            }
            else
            {
                gui->PushStyleColor(ImGuiCol_Separator, ToVec4(color));
                gui->Separator();
                gui->PopStyleColor();
            }
        }

        void Bar(IGuiManager* gui, const Palette& c, float frac, const Rgba& fill, float height, const char* overlay)
        {
            gui->PushStyleColor(ImGuiCol_PlotHistogram, ToVec4(fill));
            gui->PushStyleColor(ImGuiCol_FrameBg, ToVec4(art::twilight_dark));
            gui->PushStyleColor(ImGuiCol_Border, ToVec4(Rgba{art::amber.r, art::amber.g, art::amber.b, 0.45f}));
            gui->PushStyleColor(ImGuiCol_Text, ToVec4(c.text));
            gui->PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            gui->PushStyleVar(ImGuiStyleVar_FrameRounding, c.squared ? 0.0f : 3.0f);
            gui->ProgressBar(std::clamp(frac, 0.0f, 1.0f), ImVec2(-1, height), overlay);
            gui->PopStyleVar(2);
            gui->PopStyleColor(4);
        }

        void ImageAt(IGuiManager* gui, IDirect3DTexture8* tex, const ImVec2& pos, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1,
            const Rgba& tint, float alpha)
        {
            if (tex == nullptr || size.x <= 0.0f || size.y <= 0.0f)
                return;
            const ImVec2 keep = gui->GetCursorScreenPos();
            gui->SetCursorScreenPos(pos);
            gui->ImageWithBg(Tex(tex), size, uv0, uv1, ImVec4(0, 0, 0, 0), ImVec4(tint.r, tint.g, tint.b, tint.a * alpha));
            gui->SetCursorScreenPos(keep);
        }

        auto NineSlice(IGuiManager* gui, IDirect3DTexture8* tex, UINT tw, UINT th, const ImVec2& pos, const ImVec2& size, float srcMargin,
            float dstMargin, const Rgba& tint, float alpha) -> bool
        {
            if (tex == nullptr || tw == 0 || th == 0)
                return false;
            const float m  = std::min(dstMargin, std::min(size.x, size.y) * 0.5f);
            const float u  = srcMargin / static_cast<float>(tw);
            const float v  = srcMargin / static_cast<float>(th);
            const float xs[4] = {pos.x, pos.x + m, pos.x + size.x - m, pos.x + size.x};
            const float ys[4] = {pos.y, pos.y + m, pos.y + size.y - m, pos.y + size.y};
            const float us[4] = {0.0f, u, 1.0f - u, 1.0f};
            const float vs[4] = {0.0f, v, 1.0f - v, 1.0f};
            for (int r = 0; r < 3; ++r)
            {
                for (int col = 0; col < 3; ++col)
                {
                    ImageAt(gui, tex, ImVec2(xs[col], ys[r]), ImVec2(xs[col + 1] - xs[col], ys[r + 1] - ys[r]), ImVec2(us[col], vs[r]),
                        ImVec2(us[col + 1], vs[r + 1]), tint, alpha);
                }
            }
            return true;
        }

        auto DockWindow(IGuiManager* gui, bool docked, const Rect& dock, const Rect& user, bool locked) -> ImGuiWindowFlags
        {
            ImGuiWindowFlags f = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
            if (docked && dock.valid())
            {
                gui->SetNextWindowPos(ImVec2(dock.x, dock.y), ImGuiCond_Always);
                gui->SetNextWindowSize(ImVec2(dock.w, dock.h), ImGuiCond_Always);
                f |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            }
            else
            {
                if (user.valid())
                {
                    gui->SetNextWindowPos(ImVec2(user.x, user.y), ImGuiCond_Appearing);
                    gui->SetNextWindowSize(ImVec2(user.w, user.h), ImGuiCond_Appearing);
                }
                if (locked)
                    f |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
            }
            return f;
        }

        auto ReadBack(IGuiManager* gui, Rect& user) -> bool
        {
            const ImVec2 p = gui->GetWindowPos();
            const ImVec2 s = gui->GetWindowSize();
            const bool changed = std::abs(p.x - user.x) > 0.5f || std::abs(p.y - user.y) > 0.5f || std::abs(s.x - user.w) > 0.5f ||
                                 std::abs(s.y - user.h) > 0.5f;
            user = Rect{p.x, p.y, s.x, s.y};
            return changed;
        }

        void SetF(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, float v)
        {
            char buf[32]{};
            ::sprintf_s(buf, "%.3f", static_cast<double>(v));
            cfg->SetValue(alias, section, key, buf);
        }
        void SetI(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, int v)
        {
            char buf[32]{};
            ::sprintf_s(buf, "%d", v);
            cfg->SetValue(alias, section, key, buf);
        }
        void SetB(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, bool v)
        {
            cfg->SetValue(alias, section, key, v ? "1" : "0");
        }
        auto GetS(IConfigurationManager* cfg, const char* alias, const char* section, const char* key, const std::string& def) -> std::string
        {
            const char* v = cfg->GetString(alias, section, key);
            return (v != nullptr && v[0] != '\0') ? std::string(v) : def;
        }

        void SaveRect(IConfigurationManager* cfg, const char* alias, const char* section, const char* prefix, const Rect& r)
        {
            const std::string p(prefix);
            SetF(cfg, alias, section, (p + "X").c_str(), r.x);
            SetF(cfg, alias, section, (p + "Y").c_str(), r.y);
            SetF(cfg, alias, section, (p + "W").c_str(), r.w);
            SetF(cfg, alias, section, (p + "H").c_str(), r.h);
        }
        void LoadRect(IConfigurationManager* cfg, const char* alias, const char* section, const char* prefix, Rect& r)
        {
            const std::string p(prefix);
            r.x = cfg->GetFloat(alias, section, (p + "X").c_str(), r.x);
            r.y = cfg->GetFloat(alias, section, (p + "Y").c_str(), r.y);
            r.w = cfg->GetFloat(alias, section, (p + "W").c_str(), r.w);
            r.h = cfg->GetFloat(alias, section, (p + "H").c_str(), r.h);
        }

        auto ColorToHex(const Rgba& c) -> std::string
        {
            auto b = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            char buf[16]{};
            ::sprintf_s(buf, "%02X%02X%02X%02X", b(c.r), b(c.g), b(c.b), b(c.a));
            return buf;
        }
        auto HexToColor(const std::string& s, Rgba& out) -> bool
        {
            if (s.size() != 6 && s.size() != 8)
                return false;
            char* end        = nullptr;
            const unsigned long v = std::strtoul(s.c_str(), &end, 16);
            if (end == nullptr || *end != '\0')
                return false;
            if (s.size() == 6)
                out = hex(static_cast<uint32_t>(v));
            else
                out = Rgba{static_cast<float>((v >> 24) & 0xFF) / 255.0f, static_cast<float>((v >> 16) & 0xFF) / 255.0f,
                    static_cast<float>((v >> 8) & 0xFF) / 255.0f, static_cast<float>(v & 0xFF) / 255.0f};
            return true;
        }
    } // namespace ui
} // namespace farplaneui
