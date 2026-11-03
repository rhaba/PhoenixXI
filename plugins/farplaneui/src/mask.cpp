/**
 * Farplane UI - the UI mask.
 *
 * Port of farplaneui.lua 1.6.4-sending (band, pyreflies), fpsprites.lua (ID3DXSprite batch of
 * white PNGs tinted per draw) and fplayer.lua (draw the mask right before the game's first
 * interface draw so it sits under the game UI). Math and constants are kept 1:1 with the Lua.
 */

#include "mask.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace farplaneui
{
    namespace
    {
        constexpr int kTrailSamples   = 40;   // fixed draw budget, independent of frame rate
        constexpr float kTrailSeconds = 1.65f;
        constexpr float kPi           = std::numbers::pi_v<float>;

        constexpr DWORD kFvfPositionMask = 0x00E;
        constexpr DWORD kFvfXyzRhw       = 0x004;

        constexpr const char* kTexNames[] = {"white", "vgrad_up", "vgrad_down", "hfade", "mote", "dot", "ring"};
        static_assert(sizeof(kTexNames) / sizeof(kTexNames[0]) == static_cast<size_t>(Tex::Count));

        auto Mix(const Rgba& a, const Rgba& b, float t) -> Rgba
        {
            return Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0f};
        }

        // Pastel rainbow: hue 0..1 -> rgb, softened toward white (farplaneui.lua pastel()).
        auto Pastel(float hue, float soft) -> Rgba
        {
            hue          = hue - std::floor(hue);
            const int i  = static_cast<int>(std::floor(hue * 6.0f));
            const float f = hue * 6.0f - static_cast<float>(i);
            const float q = 1.0f - f, t = f;
            float r = 0, g = 0, b = 0;
            switch (i)
            {
                case 0: r = 1; g = t; b = 0; break;
                case 1: r = q; g = 1; b = 0; break;
                case 2: r = 0; g = 1; b = t; break;
                case 3: r = 0; g = q; b = 1; break;
                case 4: r = t; g = 0; b = 1; break;
                default: r = 1; g = 0; b = q; break;
            }
            return Rgba{r + (1 - r) * soft, g + (1 - g) * soft, b + (1 - b) * soft, 1.0f};
        }

        auto Argb(const Rgba& c, float alpha) -> D3DCOLOR
        {
            auto toByte = [](float v) { return static_cast<DWORD>(std::floor(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); };
            return D3DCOLOR_ARGB(toByte(c.a * alpha), toByte(c.r), toByte(c.g), toByte(c.b));
        }

        // flyPos(): a looping rise, side-to-side sway plus a smaller, faster circle.
        void FlyPos(const Fly& f, float t, float& x, float& y)
        {
            x = f.x + std::sin(t * f.freq + f.phase) * f.sway + std::cos(t * f.freq * 2.3f + f.phase) * f.sway * 0.25f;
            y = f.y + std::sin(t * f.freq * 2.3f + f.phase) * f.sway * 0.12f;
        }
    } // namespace

    auto Mask::Initialize(IDirect3DDevice8* device, const std::string& assetDir) -> bool
    {
        this->Release();
        device_   = device;
        assetDir_ = assetDir;
        if (device_ == nullptr)
            return false;

        if (FAILED(::D3DXCreateSprite(device_, &sprite_)))
            sprite_ = nullptr;

        loaded_ = 0;
        for (size_t i = 0; i < textures_.size(); ++i)
        {
            const std::string path = assetDir_ + kTexNames[i] + ".png";
            D3DXIMAGE_INFO info{};
            IDirect3DTexture8* tex = nullptr;
            const HRESULT hr = ::D3DXCreateTextureFromFileExA(device_, path.c_str(), D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0x00000000, &info, nullptr, &tex);
            textures_[i].hr = hr;
            if (SUCCEEDED(hr) && tex != nullptr)
            {
                // Use the real texture size (D3DX may round up to a power of two).
                D3DSURFACE_DESC desc{};
                tex->GetLevelDesc(0, &desc);
                textures_[i].tex      = tex;
                textures_[i].w        = desc.Width ? desc.Width : info.Width;
                textures_[i].h        = desc.Height ? desc.Height : info.Height;
                textures_[i].fromFile = true;
                ++loaded_;
            }
            else
            {
                // PNG missing: build an equivalent texture in memory so the mask still draws.
                MakeFallback(i, textures_[i]);
            }
        }

        if (FAILED(device_->CreateStateBlock(D3DSBT_ALL, &stateBlock_)))
            stateBlock_ = 0;

        RefreshScreenSize();
        return sprite_ != nullptr && loaded_ == static_cast<int>(textures_.size());
    }

    auto Mask::TexName(size_t i) const -> const char*
    {
        return i < textures_.size() ? kTexNames[i] : "?";
    }

    // Procedural stand-ins for the PNGs (white alpha shapes, tinted per draw like the originals).
    auto Mask::MakeFallback(size_t i, Texture& out) -> bool
    {
        const auto t = static_cast<Tex>(i);
        UINT w = 8, h = 8;
        if (t == Tex::VGradUp || t == Tex::VGradDown) { w = 8; h = 64; }
        else if (t == Tex::HFade) { w = 512; h = 4; }
        else if (t == Tex::Mote) { w = h = 32; }
        else if (t == Tex::Dot) { w = h = 16; }
        else if (t == Tex::Ring) { w = h = 64; }

        IDirect3DTexture8* tex = nullptr;
        if (FAILED(device_->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)) || tex == nullptr)
            return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(tex->LockRect(0, &lr, nullptr, 0)))
        {
            tex->Release();
            return false;
        }
        for (UINT y = 0; y < h; ++y)
        {
            auto* row = reinterpret_cast<DWORD*>(static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(y) * static_cast<size_t>(lr.Pitch));
            for (UINT x = 0; x < w; ++x)
            {
                const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
                const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
                const float dx = fx * 2.0f - 1.0f, dy = fy * 2.0f - 1.0f;
                const float d  = std::sqrt(dx * dx + dy * dy);
                float a = 1.0f;
                switch (t)
                {
                    case Tex::VGradUp: a = fy; break;           // transparent top -> opaque bottom
                    case Tex::VGradDown: a = 1.0f - fy; break;  // opaque top -> transparent bottom
                    case Tex::HFade: a = 1.0f - std::fabs(dx); break;
                    case Tex::Mote: a = std::pow(std::max(0.0f, 1.0f - d), 2.0f); break;
                    case Tex::Dot: a = std::clamp((1.0f - d) * 4.0f, 0.0f, 1.0f); break;
                    case Tex::Ring: a = std::max(0.0f, 1.0f - std::fabs(d - 0.85f) * 12.0f); break;
                    default: a = 1.0f; break;
                }
                const DWORD ab = static_cast<DWORD>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
                row[x]         = (ab << 24) | 0x00FFFFFFu;
            }
        }
        tex->UnlockRect(0);
        out.tex      = tex;
        out.w        = w;
        out.h        = h;
        out.fromFile = false;
        return true;
    }

    void Mask::Release(void)
    {
        if (image_ != nullptr)
            image_->Release();
        image_ = nullptr;
        imageFile_.clear();
        for (auto& t : textures_)
        {
            if (t.tex != nullptr)
                t.tex->Release();
            t = Texture{};
        }
        if (sprite_ != nullptr)
        {
            sprite_->Release();
            sprite_ = nullptr;
        }
        if (stateBlock_ != 0 && device_ != nullptr)
            device_->DeleteStateBlock(stateBlock_);
        stateBlock_ = 0;
        loaded_     = 0;
        device_     = nullptr;
    }

    auto Mask::Now(void) const -> double
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

    auto Mask::Rand(void) -> float
    {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_);
    }

    void Mask::RefreshScreenSize(void)
    {
        if (device_ == nullptr)
            return;
        IDirect3DSurface8* s = nullptr;
        if (SUCCEEDED(device_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &s)) && s != nullptr)
        {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(s->GetDesc(&d)) && d.Width > 0 && d.Height > 0)
            {
                screenW_ = d.Width;
                screenH_ = d.Height;
            }
            s->Release();
        }
    }

    auto Mask::BandHeight(const Settings& s) const -> int
    {
        return s.lines * s.lineHeight + s.padding;
    }

    auto Mask::BottomGap(const Settings& s, float h) const -> int
    {
        if (s.bottomGapAuto) // inputBarHeight(): 3.6% of the screen, at least 36px
            return std::max(36, static_cast<int>(std::floor(h * 0.036f + 0.5f)));
        return std::max(0, s.bottomGap);
    }

    // -------------------------------------------------------------------------------------------
    // Layer detection (fplayer.lua)
    // -------------------------------------------------------------------------------------------

    auto Mask::CurrentFvf(DWORD& fvf) const -> bool
    {
        DWORD v = 0;
        if (FAILED(device_->GetVertexShader(&v)))
            return false;
        if ((v & 1) == 1) // programmable vertex shader handle
            return false;
        fvf = v;
        return true;
    }

    auto Mask::TargetIsBackBuffer(void) -> bool
    {
        if (!backBufferRead_)
        {
            backBufferRead_ = true;
            backBuffer_     = nullptr;
            IDirect3DSurface8* s = nullptr;
            if (SUCCEEDED(device_->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &s)) && s != nullptr)
            {
                backBuffer_ = s; // compared by address only
                s->Release();
            }
        }
        if (backBuffer_ == nullptr)
            return false;

        IDirect3DSurface8* rt = nullptr;
        if (FAILED(device_->GetRenderTarget(&rt)) || rt == nullptr)
            return false;
        const bool same = (rt == backBuffer_);
        rt->Release();
        return same;
    }

    auto Mask::TextureIsRenderTarget(void) const -> bool
    {
        IDirect3DBaseTexture8* base = nullptr;
        if (FAILED(device_->GetTexture(0, &base)) || base == nullptr)
            return false;
        bool rt = false;
        if (base->GetType() == D3DRTYPE_TEXTURE)
        {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(static_cast<IDirect3DTexture8*>(base)->GetLevelDesc(0, &d)))
                rt = (d.Usage & D3DUSAGE_RENDERTARGET) != 0;
        }
        base->Release();
        return rt;
    }

    void Mask::NewFrame(void)
    {
        drawnThisFrame_ = false;
        worldCopied_    = false;
        worldDirect_    = false;
        backBufferRead_ = false;

        const double t = Now();
        if (t - lastSizeCheck_ > 5.0) // re-read the back buffer size now and then (resizes)
        {
            lastSizeCheck_ = t;
            RefreshScreenSize();
        }
    }

    void Mask::OnDrawCall(const Settings& s, const Palette& c, bool hidden, int targetIsBackBuffer)
    {
        if (inMaskDraw_ || device_ == nullptr)
            return; // our own sprite draws come back through the hook
        ++stats_.calls;
        if (drawnThisFrame_ || !s.UnderUi())
            return;

        DWORD fvf   = 0;
        const bool fixed = CurrentFvf(fvf);
        // Known from the SetRenderTarget hook when available (no per-draw D3D queries).
        if (targetIsBackBuffer == 0 || (targetIsBackBuffer < 0 && !TargetIsBackBuffer()))
            return;

        const bool rhw = fixed && (fvf & kFvfPositionMask) == kFvfXyzRhw;
        if (!rhw)
        {
            worldDirect_ = true;
            return;
        }
        if (!worldCopied_ && !worldDirect_)
        {
            // Not drawing yet: this is the copy of the rendered world (or something before it).
            if (TextureIsRenderTarget())
                worldCopied_ = true;
            return;
        }
        DrawOnce(true, s, c, hidden);
    }

    void Mask::OnEndScene(bool isRenderingBackBuffer, const Settings& s, const Palette& c, bool hidden)
    {
        ++stats_.endscene;
        if (!isRenderingBackBuffer)
            return;
        ++stats_.endsceneBack;
        DrawOnce(false, s, c, hidden); // fallback, and the 'over_ui' mode
    }

    void Mask::DrawOnce(bool under, const Settings& s, const Palette& c, bool hidden)
    {
        if (drawnThisFrame_ || sprite_ == nullptr)
            return;
        drawnThisFrame_ = true;

        if (!s.enabled || hidden)
        {
            ++stats_.hidden;
            top_ = -1.0f;
            // Band off: the dock still draws (under the UI) unless hidden for a cutscene.
            if (!hidden && !overlay_.empty() && SUCCEEDED(sprite_->Begin()))
            {
                inMaskDraw_ = true;
                if (stateBlock_ != 0)
                    device_->CaptureStateBlock(stateBlock_);
                DrawOverlay();
                sprite_->End();
                if (stateBlock_ != 0)
                    device_->ApplyStateBlock(stateBlock_);
                inMaskDraw_ = false;
            }
            return;
        }
        ++stats_.draws;
        if (under)
            ++stats_.under;

        inMaskDraw_ = true;
        if (stateBlock_ != 0)
            device_->CaptureStateBlock(stateBlock_);
        DrawBand(s, c);
        if (stateBlock_ != 0)
            device_->ApplyStateBlock(stateBlock_); // hand the game back exactly the state it set
        inMaskDraw_ = false;
    }

    // -------------------------------------------------------------------------------------------
    // Sprites (fpsprites.lua)
    // -------------------------------------------------------------------------------------------

    void Mask::Draw(Tex t, float x, float y, float w, float h, const Rgba& c, float alpha)
    {
        const auto& tx = textures_[static_cast<size_t>(t)];
        if (tx.tex == nullptr || w <= 0.0f || h <= 0.0f)
            return;
        const RECT rect{0, 0, static_cast<LONG>(tx.w), static_cast<LONG>(tx.h)};
        const D3DXVECTOR2 scale(w / static_cast<float>(tx.w), h / static_cast<float>(tx.h));
        const D3DXVECTOR2 pos(x, y);
        sprite_->Draw(tx.tex, &rect, &scale, nullptr, 0.0f, &pos, Argb(c, alpha));
    }

    // Host quads (sprite already begun). UVs are 0..1 of the image; D3DX may have padded the texture.
    void Mask::DrawOverlay(void)
    {
        for (const auto& q : overlay_)
        {
            if (q.tex == nullptr || q.w <= 0.0f || q.h <= 0.0f)
                continue;
            D3DSURFACE_DESC desc{};
            if (FAILED(q.tex->GetLevelDesc(0, &desc)) || desc.Width == 0 || desc.Height == 0)
                continue;
            const float tw = static_cast<float>(desc.Width), th = static_cast<float>(desc.Height);
            const RECT rect{static_cast<LONG>(q.u0 * tw), static_cast<LONG>(q.v0 * th), static_cast<LONG>(std::ceil(q.u1 * tw)), static_cast<LONG>(std::ceil(q.v1 * th))};
            const float sw = static_cast<float>(std::max<LONG>(1, rect.right - rect.left)), sh = static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top));
            const D3DXVECTOR2 scale(q.w / sw, q.h / sh);
            const D3DXVECTOR2 pos(q.x, q.y);
            sprite_->Draw(q.tex, &rect, &scale, nullptr, 0.0f, &pos, Argb(q.color, 1.0f));
        }
    }

    // Band background image: "auto" picks the mask_*.png whose width is closest to the screen.
    auto Mask::BandImage(const std::string& choice) -> IDirect3DTexture8*
    {
        if (choice == "off" || device_ == nullptr)
            return nullptr;
        std::string file = choice;
        // Families sized for 1024 / 1920 / 2560 wide screens; the closest width that exists is used.
        //   auto    = tall mask, 440 px at 1920 (rises past the strip panels; the default)
        //   tall500 = tall mask, 500 px at 1920
        //   short   = the original band-height mask
        if (choice == "auto" || choice == "tall500" || choice == "short")
        {
            const char* fam[3][3] = {{"mask_tall_440_1024.png", "mask_tall_440_1920.png", "mask_tall_440_2560.png"},
                {"mask_tall_500_1024.png", "mask_tall_500_1920.png", "mask_tall_500_2560.png"}, {"mask_1024.png", "mask_1920x1080.png", "mask_2560.png"}};
            const int f       = choice == "auto" ? 0 : choice == "tall500" ? 1 : 2;
            const int widths[3] = {1024, 1920, 2560};
            int best = 1 << 30;
            for (int pass = 0; pass < 2 && best == (1 << 30); ++pass) // the chosen family, else the short one
                for (int i = 0; i < 3; ++i)
                {
                    const char* name = fam[pass == 0 ? f : 2][i];
                    if (std::abs(widths[i] - static_cast<int>(screenW_)) < best && ::GetFileAttributesA((assetDir_ + "artkit\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES)
                        best = std::abs(widths[i] - static_cast<int>(screenW_)), file = name;
                }
            if (best == (1 << 30))
                return nullptr;
        }
        if (file == imageFile_)
            return image_;
        if (image_ != nullptr)
            image_->Release();
        image_     = nullptr;
        imageFile_ = file;
        D3DXIMAGE_INFO info{};
        if (SUCCEEDED(::D3DXCreateTextureFromFileExA(device_, (assetDir_ + "artkit\\" + file).c_str(), D3DX_DEFAULT, D3DX_DEFAULT, 1, 0, D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0x00000000, &info, nullptr, &image_)) && image_ != nullptr)
        {
            D3DSURFACE_DESC desc{};
            image_->GetLevelDesc(0, &desc);
            imageW_ = info.Width, imageH_ = info.Height;
            imageTexW_ = desc.Width, imageTexH_ = desc.Height;
        }
        return image_;
    }

    // -------------------------------------------------------------------------------------------
    // The band (drawBand)
    // -------------------------------------------------------------------------------------------

    void Mask::ComputeBand(const Settings& s, float& top, float& bottom) const
    {
        const float h = static_cast<float>(screenH_);
        // bandTop(): from the chat-lines setting (FancyChat following not ported yet).
        const float inputBar = static_cast<float>(std::max(36, static_cast<int>(std::floor(h * 0.036f + 0.5f))));
        const float ref      = h - inputBar - static_cast<float>(BandHeight(s));
        top                  = std::max(0.0f, std::min(h - 20.0f, ref - static_cast<float>(s.chatMargin)));
        bottom               = std::max(top + 10.0f, h - static_cast<float>(BottomGap(s, h)));
    }

    void Mask::DrawBand(const Settings& s, const Palette& c)
    {
        const float w = static_cast<float>(screenW_);
        const float h = static_cast<float>(screenH_);

        float top = 0, bottom = 0;
        ComputeBand(s, top, bottom);
        top_    = top;
        bottom_ = bottom;

        const float bh      = bottom - top;
        const float a       = std::clamp(s.opacity, 0.0f, 1.0f);
        const Rgba bodyTop  = Mix(c.abyss, c.surface2, 0.55f);
        const float fade    = static_cast<float>(s.fade);

        if (FAILED(sprite_->Begin()))
            return;

        if (IDirect3DTexture8* img = BandImage(s.maskImage); img != nullptr && imageW_ > 0)
        {
            // Background image across the full width, anchored to the bottom (its height follows the width).
            const float ih = static_cast<float>(imageH_) * (w / static_cast<float>(imageW_));
            const RECT rect{0, 0, static_cast<LONG>(imageTexW_), static_cast<LONG>(imageTexH_)};
            const D3DXVECTOR2 scale(w / static_cast<float>(imageTexW_), ih / static_cast<float>(imageTexH_));
            const D3DXVECTOR2 pos(0.0f, h - ih);
            sprite_->Draw(img, &rect, &scale, nullptr, 0.0f, &pos, Argb(Rgba{1, 1, 1, 1}, std::clamp(s.maskImageOpacity, 0.0f, 1.0f)));
        }
        else
        {
            // soft fade above the band
            if (s.fade > 0)
                Draw(Tex::VGradUp, 0, top - fade, w, fade, bodyTop, a);
            // body: charcoal base, misty slate glow toward the top, faint mist just under the edge
            Draw(Tex::White, 0, top, w, bh, c.abyss, std::min(1.0f, a + 0.06f));
            // fade out under the band's bottom edge, into the space kept for the chat input bar
            const float gap = h - bottom;
            if (gap > 0)
                Draw(Tex::VGradDown, 0, bottom, w, std::min(gap, 12.0f), c.abyss, std::min(1.0f, a + 0.06f));
            Draw(Tex::VGradDown, 0, top, w, std::max(1.0f, bh * 0.6f), bodyTop, a);
            Draw(Tex::VGradDown, 0, top, w, 18, c.border, 0.35f * a);
        }
        // accent line with a glow
        if (s.accentLine)
        {
            Draw(Tex::HFade, 0, top - 3, w, 6, c.accent(), 0.22f * a);
            Draw(Tex::HFade, 0, top - 1, w, 2, c.accent(), 0.95f);
        }

        const double t  = Now();
        const float dt  = last_ >= 0.0 ? static_cast<float>(std::clamp(t - last_, 0.0, 0.1)) : 0.0f;
        last_           = t;
        // float precision is fine for hours of uptime at the frequencies used here
        DrawFlies(s, c, w, top, bottom, dt, static_cast<float>(std::fmod(t, 100000.0)));

        // Order: mask image, Sending sprites, then the Ember dock on top of both.
        DrawOverlay();

        // Setup outline (Lua drew it with ImGui's foreground draw list; we use the same sprites).
        if (setup_)
        {
            const Rgba& o = c.warn();
            Draw(Tex::White, 1, top, w - 2, 2, o, 0.9f);
            Draw(Tex::White, 1, bottom - 2, w - 2, 2, o, 0.9f);
            Draw(Tex::White, 1, top, 2, bh, o, 0.9f);
            Draw(Tex::White, w - 3, top, 2, bh, o, 0.9f);
        }

        sprite_->End();
    }

    // -------------------------------------------------------------------------------------------
    // Pyreflies (spawnFly / drawSending / drawFlies)
    // -------------------------------------------------------------------------------------------

    auto Mask::SpawnFly(bool sending, float w, float top, float bottom, bool anywhere) -> Fly
    {
        Fly f{};
        f.x     = Rand() * w;
        f.y     = anywhere ? (top + Rand() * (bottom - top)) : (bottom + Rand() * 20.0f);
        f.speed = sending ? (16.0f + Rand() * 16.0f) : (8.0f + Rand() * 14.0f);
        f.sway  = sending ? (12.0f + Rand() * 20.0f) : (6.0f + Rand() * 14.0f);
        f.freq  = sending ? (0.35f + Rand() * 0.45f) : (0.5f + Rand() * 0.9f);
        f.phase = Rand() * kPi * 2.0f;
        f.r     = sending ? (3.1f + Rand() * 2.7f) : (1.2f + Rand() * 1.8f);
        f.hue   = Rand();
        f.warm  = Rand() < 0.35f;
        f.age   = 0.0f;
        return f;
    }

    void Mask::DrawSending(const Fly& f, float x, float y, float life, float t)
    {
        if (life <= 0.0f)
            return;
        // Evaluate the same flight curve backward in time instead of saving a point each frame,
        // so the tail has the same shape and duration at 30, 60 and 144 FPS.
        const float span  = std::min(kTrailSeconds, f.age);
        const float hue   = f.hue + t * 0.025f;
        const Rgba glow   = Pastel(hue, 0.72f);
        const Rgba veil   = Pastel(hue + 0.09f, 0.78f);
        const float pulse = 0.9f + 0.1f * std::sin(t * 1.8f + f.phase);

        for (int i = 0; i < kTrailSamples; ++i)
        {
            const float k   = static_cast<float>(i) / static_cast<float>(kTrailSamples); // tail to head
            const float lag = span * (1.0f - k);
            float px = 0, py = 0;
            FlyPos(f, t - lag, px, py);
            py += f.speed * lag;
            // Tangent of flyPos: place the veil perpendicular to the direction of flight.
            const float angle  = (t - lag) * f.freq + f.phase;
            const float orbit  = (t - lag) * f.freq * 2.3f + f.phase;
            const float vx     = std::cos(angle) * f.sway * f.freq - std::sin(orbit) * f.sway * f.freq * 2.3f * 0.25f;
            const float vy     = -f.speed + std::cos(orbit) * f.sway * f.freq * 2.3f * 0.12f;
            const float length = std::max(0.001f, std::sqrt(vx * vx + vy * vy));
            const float curl   = std::sin(lag * 3.8f + t * 0.65f + f.phase) * std::sin(kPi * k) * f.r * 1.5f;
            const float nx = -vy / length * curl, ny = vx / length * curl;
            const float size = f.r * (0.6f + 1.5f * k);
            // Scale opacity during initial growth to avoid stacking a full tail at the head.
            const float a = life * std::pow(k, 1.7f) * std::min(1.0f, span / 0.4f) * pulse;
            Draw(Tex::Mote, px - size, py - size, size * 2, size * 2, glow, a * 0.25f);
            const float soft = size * 1.3f;
            Draw(Tex::Mote, px + nx - soft, py + ny - soft, soft * 2, soft * 2, veil, a * 0.10f);
        }
        // Diffuse bloom and a pearl core; no lens ring or sharp white disc.
        const float g = f.r * 5.0f;
        Draw(Tex::Mote, x - g, y - g, g * 2, g * 2, glow, 0.24f * life * pulse);
        const float inner = f.r * 2.2f;
        Draw(Tex::Mote, x - inner, y - inner, inner * 2, inner * 2, veil, 0.60f * life * pulse);
        const float core = f.r * 1.15f;
        Draw(Tex::Mote, x - core, y - core, core * 2, core * 2, Rgba{1.0f, 0.98f, 0.93f, 1.0f}, 0.95f * life);
        const float spark = f.r * 0.32f;
        Draw(Tex::Dot, x - spark, y - spark, spark * 2, spark * 2, Rgba{1, 1, 1, 1}, 0.72f * life * pulse);
    }

    void Mask::DrawFlies(const Settings& s, const Palette& c, float w, float top, float bottom, float dt, float t)
    {
        const bool sending = s.Sending();
        if (sending != lastSending_) // style switch restarts the flock (Lua: state.flies = {})
        {
            flies_.clear();
            lastSending_ = sending;
        }
        const size_t want = s.pyreflies ? static_cast<size_t>(std::max(0, s.pyreflyCount)) : 0u;
        while (flies_.size() < want)
            flies_.push_back(SpawnFly(sending, w, top, bottom, true));
        if (flies_.size() > want)
            flies_.resize(want);

        for (auto& f : flies_)
        {
            f.y -= f.speed * dt;
            f.age += dt;
            if (f.y < top - 30.0f || f.y > bottom + 30.0f)
            {
                f = SpawnFly(sending, w, top, bottom, false);
                continue;
            }
            const float clock = sending ? f.age : t;
            float x = 0, y = 0;
            FlyPos(f, clock, x, y);
            const float life = std::max(0.0f, std::min({1.0f, (f.y - (top - 30.0f)) / 70.0f, (bottom - f.y) / 40.0f}));
            if (sending)
            {
                DrawSending(f, x, y, life, clock);
            }
            else
            {
                const float flicker = 0.75f + 0.25f * std::sin(t * 3.1f + f.phase * 2.0f);
                const Rgba& color   = f.warm ? c.warn() : c.info();
                const float g       = f.r * 3.2f;
                Draw(Tex::Mote, x - g, y - g, g * 2, g * 2, color, 0.55f * life * flicker);
                Draw(Tex::Dot, x - f.r, y - f.r, f.r * 2, f.r * 2, color, 0.9f * life * flicker);
            }
        }
    }

} // namespace farplaneui
