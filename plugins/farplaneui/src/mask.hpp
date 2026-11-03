/**
 * Farplane UI - the UI mask (port of farplaneui.lua drawBand/pyreflies, fpsprites.lua, fplayer.lua).
 */

#ifndef FARPLANEUI_MASK_HPP_INCLUDED
#define FARPLANEUI_MASK_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"
#include "settings.hpp"
#include "theme.hpp"

#include <array>
#include <chrono>
#include <random>
#include <string>
#include <vector>

namespace farplaneui
{
    enum class Tex : int
    {
        White = 0,
        VGradUp,
        VGradDown,
        HFade,
        Mote,
        Dot,
        Ring,
        Count
    };

    struct Fly
    {
        float x, y, speed, sway, freq, phase, r, hue;
        bool warm;
        float age;
    };

    struct MaskStats
    {
        uint64_t endscene{0}, endsceneBack{0}, draws{0}, under{0}, hidden{0}, errors{0}, calls{0};
    };

    // Extra textured quads drawn with the band (under the game UI and every ImGui window).
    struct SpriteQuad
    {
        IDirect3DTexture8* tex;
        float x, y, w, h, u0, v0, u1, v1;
        Rgba color;
    };

    class Mask
    {
        struct Texture
        {
            IDirect3DTexture8* tex{nullptr};
            UINT w{0}, h{0};
            bool fromFile{false};  // false = procedural fallback (PNG missing/unreadable)
            HRESULT hr{0};         // result of D3DXCreateTextureFromFileExA
        };
        auto MakeFallback(size_t i, Texture& out) -> bool;

        IDirect3DDevice8* device_{nullptr};
        ID3DXSprite* sprite_{nullptr};
        DWORD stateBlock_{0};
        std::array<Texture, static_cast<size_t>(Tex::Count)> textures_{};
        int loaded_{0};

        std::vector<Fly> flies_;
        std::mt19937 rng_{std::random_device{}()};
        std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
        double last_{-1.0};
        bool lastSending_{true};

        // Per-frame layer detection state (fplayer.lua)
        bool drawnThisFrame_{false};
        bool worldCopied_{false};
        bool worldDirect_{false};
        bool inMaskDraw_{false};
        IDirect3DSurface8* backBuffer_{nullptr}; // address only; not AddRef'd
        bool backBufferRead_{false};

        UINT screenW_{1920}, screenH_{1080};
        double lastSizeCheck_{-100.0};

    public:
        // Last drawn band edges (for the setup outline / stats). top_ < 0 when hidden.
        float top_{-1.0f}, bottom_{0.0f};
        bool setup_{false};
        MaskStats stats_{};
        std::vector<SpriteQuad> overlay_; // set by the host each frame (Ember dock)
        // Called once per frame when the 3D scene is finished, just before the mask draws (the shadow
        // effects' front batch, worldring.cpp DrawFront).
        void (*beforeDraw_)(void* user){nullptr};
        void* beforeDrawUser_{nullptr};
        std::string assetDir_;
        // Band background image (artkit\mask_*.png), loaded when the chosen file changes.
        IDirect3DTexture8* image_{nullptr};
        std::string imageFile_;
        UINT imageW_{0}, imageH_{0}, imageTexW_{0}, imageTexH_{0};
        auto BandImage(const std::string& choice) -> IDirect3DTexture8*;

        auto Initialize(IDirect3DDevice8* device, const std::string& assetDir) -> bool;
        void Release(void);
        auto Ready(void) const -> bool { return sprite_ != nullptr; }
        auto TexturesLoaded(void) const -> int { return loaded_; } // PNGs loaded from disk
        auto TextureCount(void) const -> int { return static_cast<int>(textures_.size()); }
        auto TexName(size_t i) const -> const char*;
        auto TexFromFile(size_t i) const -> bool { return textures_[i].fromFile; }
        auto TexPresent(size_t i) const -> bool { return textures_[i].tex != nullptr; }
        auto TexResult(size_t i) const -> HRESULT { return textures_[i].hr; }
        auto TexHandle(Tex t) const -> IDirect3DTexture8* { return textures_[static_cast<size_t>(t)].tex; }
        auto ScreenW(void) const -> UINT { return screenW_; }
        auto ScreenH(void) const -> UINT { return screenH_; }

        // Called from the plugin's Direct3D hooks.
        void NewFrame(void);
        void OnDrawCall(const Settings& s, const Palette& c, bool hidden, int targetIsBackBuffer = -1); // -1 = query D3D
        void OnEndScene(bool isRenderingBackBuffer, const Settings& s, const Palette& c, bool hidden);

        auto BandHeight(const Settings& s) const -> int;
        auto BottomGap(const Settings& s, float h) const -> int;
        void ResetFlies(void) { flies_.clear(); }
        void ComputeBand(const Settings& s, float& top, float& bottom) const;

    private:
        auto Now(void) const -> double;
        auto Rand(void) -> float;
        auto CurrentFvf(DWORD& fvf) const -> bool;
        auto TargetIsBackBuffer(void) -> bool;
        auto TextureIsRenderTarget(void) const -> bool;
        void RefreshScreenSize(void);

        void DrawOnce(bool under, const Settings& s, const Palette& c, bool hidden);
        void DrawOverlay(void);
        void DrawBand(const Settings& s, const Palette& c);
        void DrawFlies(const Settings& s, const Palette& c, float w, float top, float bottom, float dt, float t);
        void DrawSending(const Fly& f, float x, float y, float life, float t);
        auto SpawnFly(bool sending, float w, float top, float bottom, bool anywhere) -> Fly;
        void Draw(Tex t, float x, float y, float w, float h, const Rgba& c, float alpha);
    };

} // namespace farplaneui

#endif // FARPLANEUI_MASK_HPP_INCLUDED
