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
        if ((!ring.want && !ctx_->shadows.want) || ring.drawnFrame == cam.frame || cam.lastFrame != cam.frame || cam.sceneTarget == nullptr ||
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

        if (ctx_->ring.want)
            DrawRing();
        if (ctx_->shadows.want)
            DrawShadows();
        device_->ApplyStateBlock(stateBlock_); // hand the game back exactly the state it had
    }

    void WorldRingRenderer::DrawRing(void)
    {
        const auto& ring = ctx_->ring;
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
    }

    // Utsusemi shadows (incoming ember_dock_wiring (2) WIRING.md U3, U7). Drawn in the 3D scene with the
    // depth test on, so the character really hides the parts behind it. Sizes given in px are 1080p
    // screen pixels at the player's distance, converted with pw (yalms per pixel).
    void WorldRingRenderer::DrawShadows(void)
    {
        const auto& sh  = ctx_->shadows;
        const auto& v   = ctx_->camera.view;
        struct V3
        {
            float x, y, z;
        };
        auto add   = [](V3 a, V3 b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; };
        auto sub   = [](V3 a, V3 b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; };
        auto mul   = [](V3 a, float k) { return V3{a.x * k, a.y * k, a.z * k}; };
        auto cross = [](V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        auto len   = [](V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); };
        auto norm  = [&](V3 a) { const float l = len(a); return l > 1e-5f ? mul(a, 1.0f / l) : V3{0, 0, 0}; };
        // Camera axes and position from the view matrix (rows of its rotation part).
        const V3 camR{v._11, v._21, v._31}, camU{v._12, v._22, v._32}, camF{v._13, v._23, v._33};
        const V3 camPos = mul(add(add(mul(camR, v._41), mul(camU, v._42)), mul(camF, v._43)), -1.0f);
        const V3 up{0, -1, 0}; // game world: up is -Y
        const float h = sh.height * sh.size * sh.outScale;
        const V3 feet{sh.x, sh.y - 0.04f, sh.z};
        const V3 chest = add(feet, mul(up, 0.55f * sh.height));
        const float projY = std::abs(ctx_->camera.proj._22) > 0.01f ? std::abs(ctx_->camera.proj._22) : 1.0f;
        const float pw    = 2.0f * len(sub(chest, camPos)) / (projY * 1080.0f);
        const float A     = std::clamp(sh.opacity * sh.outA, 0.0f, 1.0f);
        const Rgba hot{1.0f, 0.47f, 0.24f, 1}, ivory{0.95f, 0.91f, 0.85f, 1}, grey{0.55f, 0.58f, 0.66f, 1}, dark{0.06f, 0.07f, 0.11f, 1};
        const Rgba& col = sh.color;
        const int n     = std::max(1, sh.maxShown);

        auto tri = [&](V3 a, V3 b, V3 c, D3DCOLOR k) {
            const Vertex t[3] = {{a.x, a.y, a.z, k}, {b.x, b.y, b.z, k}, {c.x, c.y, c.z, k}};
            device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, t, sizeof(Vertex));
        };
        // Camera-facing line of `wPx` screen pixels.
        auto line = [&](V3 a, V3 b, float wPx, D3DCOLOR k) {
            const V3 mid = mul(add(a, b), 0.5f);
            const V3 o   = mul(norm(cross(sub(b, a), sub(mid, camPos))), wPx * pw * 0.5f);
            const Vertex q[4] = {{a.x + o.x, a.y + o.y, a.z + o.z, k}, {a.x - o.x, a.y - o.y, a.z - o.z, k}, {b.x + o.x, b.y + o.y, b.z + o.z, k},
                {b.x - o.x, b.y - o.y, b.z - o.z, k}};
            device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vertex));
        };
        auto glowLine = [&](V3 a, V3 b, const Rgba& c, float a0) {
            if (sh.glow)
                line(a, b, 4.0f, Argb(c, a0 * 0.18f));
            line(a, b, 1.5f, Argb(c, a0));
        };
        // Camera-facing soft dot: centre colour fading to the rim colour (radius in world units).
        auto dot = [&](V3 p, float r, D3DCOLOR cIn, D3DCOLOR cOut) {
            constexpr int kSeg = 16;
            Vertex fan[kSeg + 2];
            fan[0] = {p.x, p.y, p.z, cIn};
            for (int i = 0; i <= kSeg; ++i)
            {
                const float a = static_cast<float>(i) * kTwoPi / kSeg, ca = std::cos(a) * r, sa = std::sin(a) * r;
                fan[i + 1] = {p.x + camR.x * ca + camU.x * sa, p.y + camR.y * ca + camU.y * sa, p.z + camR.z * ca + camU.z * sa, cOut};
            }
            device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, kSeg, fan, sizeof(Vertex));
        };
        // Diamond: camera-facing (ground = false) or flat on the ground (ground = true), half size r.
        auto diamond = [&](V3 p, float r, D3DCOLOR k, bool ground) {
            const V3 ax = ground ? V3{r, 0, 0} : mul(camR, r), ay = ground ? V3{0, 0, r} : mul(camU, r);
            tri(add(p, ay), add(p, ax), sub(p, ay), k);
            tri(add(p, ay), sub(p, ax), sub(p, ay), k);
        };
        // Per-slot state: lit, the one just lost (flash), or empty.
        auto lit  = [&](int k) { return k < sh.count; };
        auto lost = [&](int k) { return k == sh.lostIdx && sh.lostA > 0.0f; };
        const float litA = A * sh.inA;

        switch (sh.style)
        {
        case 1: // wisps: orbiting motes with a fading trail, a faint ground ring
        {
            const float R = 0.5f * h;
            Strip(feet.x, feet.y, feet.z, R - 1.5f * pw, R + 1.5f * pw, 0.0f, kTwoPi, Argb(col, 0.43f * A), Argb(col, 0.43f * A));
            for (int k = 0; k < n; ++k)
            {
                const float phi = sh.time + kTwoPi * static_cast<float>(k) / static_cast<float>(n);
                auto at = [&](float a, float r) { return add(chest, V3{std::cos(a) * r, 0, std::sin(a) * r}); };
                if (lost(k))
                {
                    const V3 p = at(phi, R * (1.0f + 0.6f * (1.0f - sh.lostA)));
                    dot(p, 9.0f * pw * sh.size, Argb(hot, sh.lostA * A), Argb(hot, 0.0f));
                    continue;
                }
                if (!lit(k))
                {
                    dot(at(phi, R), 2.5f * pw, Argb(grey, 0.3f * A), Argb(grey, 0.0f));
                    continue;
                }
                for (int j = 6; j >= 1; --j) // trail along the past angles
                    dot(at(phi - 0.12f * static_cast<float>(j), R), 2.0f * pw * sh.size, Argb(col, litA * 0.5f * (1.0f - j / 7.0f)), Argb(col, 0.0f));
                const V3 p = at(phi, R);
                if (sh.glow)
                    dot(p, 12.0f * pw * sh.size, Argb(Rgba{0.10f, 0.08f, 0.20f, 1}, 0.59f * litA), Argb(col, 0.0f));
                dot(p, 6.5f * pw * sh.size, Argb(col, litA), Argb(col, 0.0f));
                dot(p, 2.5f * pw * sh.size, Argb(ivory, litA), Argb(ivory, 0.3f * litA));
            }
            break;
        }
        case 2: // diamond: a slowly turning octahedron, one side facet per shadow
        {
            const float R = 0.42f * h;
            const V3 top = add(chest, mul(up, 0.62f * h)), bot = add(chest, mul(up, -0.58f * h));
            std::vector<V3> e(static_cast<size_t>(n));
            for (int k = 0; k < n; ++k)
            {
                const float a = sh.time + kTwoPi * static_cast<float>(k) / static_cast<float>(n);
                e[static_cast<size_t>(k)] = add(chest, V3{std::cos(a) * R, 0, std::sin(a) * R});
            }
            for (int k = 0; k < n; ++k)
            {
                const V3 a = e[static_cast<size_t>(k)], b = e[static_cast<size_t>((k + 1) % n)];
                if (lit(k) || lost(k))
                {
                    const Rgba& c = lost(k) ? hot : col;
                    const float fa = lost(k) ? 0.45f * sh.lostA * A : 0.06f * litA; // faint fill keeps the character visible
                    tri(top, a, b, Argb(c, fa));
                    tri(bot, a, b, Argb(c, fa));
                }
            }
            for (int k = 0; k < n; ++k)
            {
                const int kp      = (k + n - 1) % n;
                const V3 a        = e[static_cast<size_t>(k)], b = e[static_cast<size_t>((k + 1) % n)];
                const bool on     = lit(k), onSide = lit(k) || lit(kp);
                const bool flash  = lost(k) || lost(kp);
                const Rgba amber{0.90f, 0.71f, 0.47f, 1};
                // equator edge of facet k, and the vertical edges through e_k (shared with facet k-1)
                if (lost(k))
                    glowLine(a, b, hot, sh.lostA * A);
                else if (on)
                    glowLine(a, b, amber, 0.9f * litA);
                else
                    line(a, b, 1.0f, Argb(grey, 0.27f * A));
                const Rgba& sc = flash ? hot : amber;
                const float sa = flash ? sh.lostA * A : onSide ? 0.9f * litA : 0.0f;
                if (sa > 0.0f)
                {
                    glowLine(top, a, sc, sa);
                    glowLine(bot, a, sc, sa);
                }
                else
                {
                    line(top, a, 1.0f, Argb(grey, 0.27f * A));
                    line(bot, a, 1.0f, Argb(grey, 0.27f * A));
                }
            }
            dot(top, 3.0f * pw, Argb(ivory, A), Argb(ivory, 0.4f * A));
            dot(bot, 3.0f * pw, Argb(ivory, A), Argb(ivory, 0.4f * A));
            break;
        }
        case 3: // rune ring: a ground circle with one rune per shadow
        {
            const float R = 0.62f * h, rr = 0.09f * h;
            Strip(feet.x, feet.y, feet.z, R - 1.0f * pw, R + 1.0f * pw, 0.0f, kTwoPi, Argb(col, 0.55f * A), Argb(col, 0.55f * A));
            if (sh.glow)
                Strip(feet.x, feet.y, feet.z, R + 1.0f * pw, R + 6.0f * pw, 0.0f, kTwoPi, Argb(col, 0.15f * A), Argb(col, 0.0f));
            for (int k = 0; k < n; ++k)
            {
                const float a = -kPi * 0.5f + kTwoPi * static_cast<float>(k) / static_cast<float>(n);
                const V3 p    = {feet.x + std::cos(a) * R, feet.y - 0.01f, feet.z + std::sin(a) * R};
                if (lost(k))
                {
                    diamond(p, rr * (1.0f + 0.8f * (1.0f - sh.lostA)), Argb(hot, sh.lostA * A), true);
                    continue;
                }
                diamond(p, rr * 1.2f, Argb(lit(k) ? col : grey, (lit(k) ? 0.35f * litA : 0.35f * A)), true);
                diamond(p, rr, Argb(lit(k) ? col : dark, lit(k) ? litA : 0.7f * A), true);
                if (lit(k))
                    diamond(p, rr * 0.4f, Argb(ivory, litA), true);
            }
            break;
        }
        case 4: // smoke: soft clone columns beside the player, a lost clone disperses
        {
            const float offs[6] = {-0.55f, 0.55f, -1.0f, 1.0f, -1.45f, 1.45f};
            // Sideways relative to the camera, flattened onto the ground.
            const V3 side = norm(V3{camR.x, 0, camR.z});
            for (int k = 0; k < std::min(n, 6); ++k)
            {
                const bool gone = lost(k);
                if (!lit(k) && !gone)
                    continue;
                const float spread = gone ? 1.0f + 1.5f * (1.0f - sh.lostA) : 1.0f;
                const float ka     = gone ? sh.lostA * A * 0.8f : litA;
                const V3 base      = add(feet, mul(side, offs[k] * h));
                constexpr int kPuffs = 14;
                for (int i = 0; i < kPuffs; ++i)
                {
                    const float t  = static_cast<float>(i) / static_cast<float>(kPuffs - 1);
                    const float ph = sh.time * 2.0f + static_cast<float>(i * 7 + k * 13);
                    const V3 jit{std::sin(ph) * 0.05f * h * spread, 0, std::cos(ph * 1.3f) * 0.05f * h * spread};
                    const V3 p = add(add(base, mul(up, (0.08f + 0.8f * t) * h)), jit);
                    const float r = (0.13f + 0.05f * std::sin(ph * 0.7f)) * h * spread;
                    dot(p, r, Argb(col, 0.3f * ka), Argb(col, 0.0f));
                }
                dot(add(base, mul(up, 0.95f * h)), 0.12f * h * spread, Argb(dark, 0.45f * ka), Argb(dark, 0.0f)); // head
                Disk(base.x, base.y, base.z, 0.22f * h * spread, Argb(dark, 0.25f * ka)); // smoke pool
            }
            break;
        }
        case 5: // pips: a row of small diamonds above the head
        {
            const V3 anchor = add(feet, mul(up, 1.18f * sh.height));
            const float step = 24.0f * pw * sh.size, r = 7.0f * pw * sh.size;
            for (int k = 0; k < n; ++k)
            {
                const V3 p = add(anchor, mul(camR, (static_cast<float>(k) - (n - 1) * 0.5f) * step));
                if (lost(k))
                {
                    diamond(p, r * (1.0f + 0.6f * (1.0f - sh.lostA)), Argb(hot, sh.lostA * A), false);
                    continue;
                }
                if (lit(k) && sh.glow)
                    diamond(p, r * 1.6f, Argb(col, 0.18f * litA), false);
                diamond(p, r, lit(k) ? Argb(col, litA) : Argb(dark, 0.75f * A), false);
                diamond(p, r * 0.45f, lit(k) ? Argb(ivory, litA) : Argb(grey, 0.45f * A), false);
            }
            break;
        }
        default:
            break;
        }
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
