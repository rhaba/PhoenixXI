/**
 * Farplane UI - target ground ring drawn inside the 3D scene (see worldring.hpp).
 */

#include "worldring.hpp"

#include <algorithm>
#include <cmath>

namespace farplaneui
{
    namespace
    {
        constexpr UINT kSetRenderTargetSlot = 31; // IDirect3DDevice8 vtable index
        using SetRenderTarget_f = HRESULT(__stdcall*)(IDirect3DDevice8*, IDirect3DSurface8*, IDirect3DSurface8*);

        WorldRingRenderer* g_ring       = nullptr;
        SetRenderTarget_f g_original    = nullptr;
        void** g_vtable                 = nullptr;

        struct Vertex
        {
            float x, y, z;
            D3DCOLOR color;
        };
        constexpr DWORD kFvf     = D3DFVF_XYZ | D3DFVF_DIFFUSE;
        constexpr int kSegments  = 96;
        constexpr float kTwoPi   = 6.28318531f;
        constexpr float kPi      = 3.14159265f;

        auto Argb(const Rgba& c, float alpha) -> D3DCOLOR
        {
            const auto b = [](float v) { return static_cast<DWORD>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            return D3DCOLOR_ARGB(b(c.a * alpha), b(c.r), b(c.g), b(c.b));
        }

        auto PatchSlot(void** slot, void* value) -> bool
        {
            DWORD old = 0;
            if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
                return false;
            *slot = value;
            ::VirtualProtect(slot, sizeof(void*), old, &old);
            return true;
        }
    } // namespace

    auto WorldRingRenderer::Install(IDirect3DDevice8* device, Context* ctx) -> bool
    {
        if (device == nullptr || ctx == nullptr || g_ring != nullptr)
            return false;
        device_ = device;
        ctx_    = ctx;
        g_vtable = *reinterpret_cast<void***>(device);
        g_original = reinterpret_cast<SetRenderTarget_f>(g_vtable[kSetRenderTargetSlot]);
        g_ring     = this;
        if (!PatchSlot(&g_vtable[kSetRenderTargetSlot], reinterpret_cast<void*>(&WorldRingRenderer::SetRenderTargetHook)))
        {
            g_ring = nullptr;
            return false;
        }
        ctx_->ring.hooked = true;
        return true;
    }

    void WorldRingRenderer::Uninstall(void)
    {
        // Only restore if the slot is still ours (another plugin may have hooked on top of us).
        if (g_vtable != nullptr && g_vtable[kSetRenderTargetSlot] == reinterpret_cast<void*>(&WorldRingRenderer::SetRenderTargetHook))
            PatchSlot(&g_vtable[kSetRenderTargetSlot], reinterpret_cast<void*>(g_original));
        if (stateBlock_ != 0 && device_ != nullptr)
            device_->DeleteStateBlock(stateBlock_);
        stateBlock_ = 0;
        if (ctx_ != nullptr)
            ctx_->ring.hooked = false;
        g_ring   = nullptr;
        g_vtable = nullptr;
        device_  = nullptr;
        ctx_     = nullptr;
    }

    auto __stdcall WorldRingRenderer::SetRenderTargetHook(IDirect3DDevice8* self, IDirect3DSurface8* target, IDirect3DSurface8* depth) -> HRESULT
    {
        if (g_ring != nullptr)
            g_ring->OnSetRenderTarget(self, target);
        return g_original(self, target, depth);
    }

    // The world is finished when the game switches away from the scene target it was drawn into.
    void WorldRingRenderer::OnSetRenderTarget(IDirect3DDevice8* self, IDirect3DSurface8* target)
    {
        if (ctx_ != nullptr && self == device_ && target != nullptr && ctx_->backBuffer != nullptr)
        {
            ctx_->targetIsBackBuffer = target == ctx_->backBuffer;
            ctx_->targetKnown        = true;
        }
        if (drawing_ || self != device_ || ctx_ == nullptr)
            return;
        auto& ring      = ctx_->ring;
        const auto& cam = ctx_->camera;
        if (!ring.want || ring.drawnFrame == cam.frame || cam.lastFrame != cam.frame || cam.sceneTarget == nullptr ||
            target == cam.sceneTarget)
            return;
        IDirect3DSurface8* current = nullptr;
        if (FAILED(device_->GetRenderTarget(&current)) || current == nullptr)
            return;
        const bool onScene = current == cam.sceneTarget;
        current->Release();
        if (!onScene)
            return;
        drawing_ = true;
        Draw();
        drawing_        = false;
        ring.drawnFrame = cam.frame;
    }

    void WorldRingRenderer::Draw(void)
    {
        const auto& ring = ctx_->ring;
        const auto& cam  = ctx_->camera;
        if (stateBlock_ == 0 && FAILED(device_->CreateStateBlock(D3DSBT_ALL, &stateBlock_)))
        {
            stateBlock_ = 0;
            return;
        }
        device_->CaptureStateBlock(stateBlock_);

        D3DMATRIX world{};
        world._11 = world._22 = world._33 = world._44 = 1.0f;
        device_->SetTransform(D3DTS_WORLD, &world);
        device_->SetTransform(D3DTS_VIEW, &cam.view);
        device_->SetTransform(D3DTS_PROJECTION, &cam.proj);
        device_->SetVertexShader(kFvf);
        device_->SetPixelShader(0);
        device_->SetTexture(0, nullptr);
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
        // Depth: test against the scene (the mob and terrain hide the ring), never write.
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ZFUNC, cam.zFunc);
        device_->SetRenderState(D3DRS_ZBIAS, 4);

        // Game world: up is -Y, so lift the ring a little off the ground to avoid z-fighting.
        const float cx = ring.x, cy = ring.y - 0.04f, cz = ring.z;
        const float r  = std::max(0.3f, ring.radius);

        if (ring.style == 2) // classic: the original wide band + arrowhead
        {
            const float band  = std::max(0.10f, r * 0.13f); // solid width
            const float soft  = band * 0.45f;               // feathered edges
            const float rIn   = r - band * 0.5f, rOut = r + band * 0.5f;
            const D3DCOLOR solid = Argb(ring.color, 0.85f), clear = Argb(ring.color, 0.0f);
            const float radii[4]     = {std::max(0.01f, rIn - soft), rIn, rOut, rOut + soft};
            const D3DCOLOR colors[4] = {clear, solid, solid, clear};
            for (int b = 0; b < 3; ++b)
                Strip(cx, cy, cz, radii[b], radii[b + 1], 0.0f, kTwoPi, colors[b], colors[b + 1]);
            if (ring.arrow)
            {
                const float fx = std::cos(ring.yaw), fz = -std::sin(ring.yaw);
                const float tx = -fz, tz = fx; // tangent
                const float base = rOut - band * 0.2f, tip = rOut + band * 2.2f, half = band * 1.6f;
                const Vertex tri[3] = {
                    {cx + fx * tip, cy, cz + fz * tip, solid},
                    {cx + fx * base + tx * half, cy, cz + fz * base + tz * half, solid},
                    {cx + fx * base - tx * half, cy, cz + fz * base - tz * half, solid},
                };
                device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(Vertex));
            }
            device_->ApplyStateBlock(stateBlock_);
            return;
        }

        // Ember ring (incoming/ember_dock3 WIRING.md T1-T7). Widths are screen px converted to world
        // units at the target (ring.pxWorld = yalms per screen pixel), so lines keep their thickness.
        const float px    = std::max(0.002f, ring.pxWorld);
        const float face  = -ring.yaw;               // angle of the facing direction in (X, Z) ring space
        const float notch = ring.notch;
        const float a0 = face + notch, a1 = face + kTwoPi - notch;
        const float alpha = std::clamp(ring.alpha, 0.0f, 1.0f);
        const float core  = ring.lineWidth * px * 0.5f; // half width
        auto glowLine = [&](float rc, float b0, float b1, const Rgba& c, float coreHalf, float a) {
            for (int i = ring.glowPasses; i >= 1; --i) // wide and faint -> narrow
            {
                const float hw = coreHalf + static_cast<float>(i) * ring.glowStep * px * 0.5f;
                const float ga = a * ring.glowAlpha / static_cast<float>(i) * 0.35f;
                Strip(cx, cy, cz, std::max(0.0f, rc - hw), rc, b0, b1, Argb(c, 0.0f), Argb(c, ga));
                Strip(cx, cy, cz, rc, rc + hw, b0, b1, Argb(c, ga), Argb(c, 0.0f));
            }
            Strip(cx, cy, cz, std::max(0.0f, rc - coreHalf), rc + coreHalf, b0, b1, Argb(c, a), Argb(c, a));
        };
        // Inner fill, very faint.
        Disk(cx, cy, cz, r, Argb(ring.color, ring.innerAlpha * alpha));
        // TP move / cast pulse: two staggered waves growing out from the ring.
        if (ring.pulseAge >= 0.0f)
            for (int k = 0; k < 2; ++k)
            {
                const float ph = std::fmod(ring.pulseAge * ring.pulseHz + static_cast<float>(k) * 0.5f, 1.0f);
                Strip(cx, cy, cz, r * (1.0f + ph * ring.pulseGrow) - px, r * (1.0f + ph * ring.pulseGrow) + px, 0.0f, kTwoPi,
                    Argb(ring.color, (1.0f - ph) * 0.6f * alpha), Argb(ring.color, (1.0f - ph) * 0.6f * alpha));
            }
        if (ring.style == 1)
        {
            // HP arc: dark track over the notched arc, the claim colour over hp, a fading ivory segment
            // for the HP just lost, an ivory end cap.
            const Rgba trackCol{0.04f, 0.05f, 0.09f, 1.0f};
            const float tw = ring.trackWidth * px * 0.5f;
            Strip(cx, cy, cz, r - tw, r + tw, a0, a1, Argb(trackCol, 0.59f * alpha), Argb(trackCol, 0.59f * alpha));
            const float span = a1 - a0;
            const float hp   = std::clamp(ring.hp, 0.0f, 1.0f), ghost = std::clamp(ring.hpGhost, hp, 1.0f);
            if (ghost > hp && ring.ghostAlpha > 0.0f)
                Strip(cx, cy, cz, r - tw, r + tw, a0 + span * hp, a0 + span * ghost, Argb(Rgba{0.95f, 0.91f, 0.85f, 1}, 0.35f * ring.ghostAlpha * alpha),
                    Argb(Rgba{0.95f, 0.91f, 0.85f, 1}, 0.35f * ring.ghostAlpha * alpha));
            if (hp > 0.0f)
            {
                glowLine(r, a0, a0 + span * hp, ring.color, ring.arcWidth * px * 0.5f, alpha);
                Strip(cx, cy, cz, r * 0.99f - px * 0.5f, r * 0.99f + px * 0.5f, a0, a0 + span * hp, Argb(Rgba{1.0f, 0.86f, 0.75f, 1}, 0.43f * alpha),
                    Argb(Rgba{1.0f, 0.86f, 0.75f, 1}, 0.43f * alpha));
                const float ea = a0 + span * hp;
                Disk(cx + std::cos(ea) * r, cy, cz + std::sin(ea) * r, 3.5f * px, Argb(Rgba{0.95f, 0.91f, 0.85f, 1}, alpha));
            }
            // Distance tick at angle pi (screen-left side of the ring).
            Strip(cx, cy, cz, r, r * 1.14f, kPi - 0.5f * px / r * 2.0f, kPi + 0.5f * px / r * 2.0f, Argb(ring.tickColor, alpha), Argb(ring.tickColor, alpha));
        }
        else
            glowLine(r, a0, a1, ring.color, core, alpha);

        // Rear arc (sneak attack zone), just inside the ring.
        if (ring.rear)
        {
            const float ra = std::clamp(ring.rearAlpha * (ring.rearIn ? ring.rearBoost : 1.0f), 0.0f, 1.0f) * alpha;
            glowLine(r * 0.95f, face + kPi - ring.rearArc, face + kPi + ring.rearArc, ring.color, 0.75f * px, ra);
        }

        // Chevron in the notch: base on the ring, tip outside, notched back.
        if (ring.arrow)
        {
            const float ux = std::cos(face), uz = std::sin(face), tx = -uz, tz = ux;
            const float L = ring.chevronLen * r, W = ring.chevronWidth * r;
            const D3DCOLOR c = Argb(ring.color, alpha);
            const Vertex tip{cx + ux * (r + L), cy, cz + uz * (r + L), c};
            const Vertex b1{cx + ux * r + tx * W, cy, cz + uz * r + tz * W, c};
            const Vertex b2{cx + ux * r - tx * W, cy, cz + uz * r - tz * W, c};
            const Vertex mid{cx + ux * (r + L * 0.35f), cy, cz + uz * (r + L * 0.35f), c};
            const Vertex tris[6] = {b1, tip, mid, mid, tip, b2};
            device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, tris, sizeof(Vertex));
        }

        device_->ApplyStateBlock(stateBlock_); // hand the game back exactly the state it had
    }

    // Ring segment between radii rIn..rOut over angles b0..b1 (colours at the inner / outer edge).
    void WorldRingRenderer::Strip(float cx, float cy, float cz, float rIn, float rOut, float b0, float b1, D3DCOLOR cIn, D3DCOLOR cOut)
    {
        if (b1 <= b0 || rOut <= rIn)
            return;
        const int n = std::clamp(static_cast<int>((b1 - b0) / kTwoPi * kSegments) + 1, 2, kSegments);
        Vertex strip[(kSegments + 1) * 2];
        for (int i = 0; i <= n; ++i)
        {
            const float a = b0 + (b1 - b0) * static_cast<float>(i) / static_cast<float>(n), ca = std::cos(a), sa = std::sin(a);
            strip[i * 2]     = {cx + ca * rIn, cy, cz + sa * rIn, cIn};
            strip[i * 2 + 1] = {cx + ca * rOut, cy, cz + sa * rOut, cOut};
        }
        device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, n * 2, strip, sizeof(Vertex));
    }
    // Filled disc (triangle fan).
    void WorldRingRenderer::Disk(float cx, float cy, float cz, float rad, D3DCOLOR c)
    {
        if (rad <= 0.0f || (c >> 24) == 0)
            return;
        Vertex fan[kSegments + 2];
        fan[0] = {cx, cy, cz, c};
        for (int i = 0; i <= kSegments; ++i)
        {
            const float a = static_cast<float>(i) * kTwoPi / kSegments;
            fan[i + 1]    = {cx + std::cos(a) * rad, cy, cz + std::sin(a) * rad, c};
        }
        device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, kSegments, fan, sizeof(Vertex));
    }
} // namespace farplaneui
