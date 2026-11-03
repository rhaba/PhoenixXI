/**
 * Farplane UI - target ground ring drawn inside the 3D scene (see worldring.hpp).
 */

#include "worldring.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

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
        std::vector<Vertex> g_front;        // shadow FRONT batch built in the scene pass, drawn by DrawFront
        uint32_t g_frontFrame = 0xFFFFFFFFu;
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
        if ((!ring.want && !ctx_->shadows.want && !ctx_->cast.want) || ring.drawnFrame == cam.frame || cam.lastFrame != cam.frame || cam.sceneTarget == nullptr ||
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
        if (stateBlock_ == 0 && FAILED(device_->CreateStateBlock(D3DSBT_ALL, &stateBlock_)))
        {
            stateBlock_ = 0;
            return;
        }
        device_->CaptureStateBlock(stateBlock_);
        SetupStates();
        if (ctx_->ring.want)
            DrawRing();
        if (ctx_->cast.want)
            DrawCast();
        if (ctx_->shadows.want)
            DrawShadows();
        device_->ApplyStateBlock(stateBlock_); // hand the game back exactly the state it had
    }

    // Screen-space draw: each vertex is projected here (the scene's view * projection onto the back buffer)
    // and drawn as a pre-transformed (XYZRHW) triangle, the same kind of draw the mask's sprites make. The
    // game's transforms and viewport are never touched; only the few states set here are saved and put back.
    void WorldRingRenderer::DrawFront(void)
    {
        if (device_ == nullptr || ctx_ == nullptr || drawing_ || g_frontFrame != ctx_->camera.frame || g_front.empty())
            return;
        float W = 0, H = 0;
        IDirect3DSurface8* rt = nullptr;
        if (SUCCEEDED(device_->GetRenderTarget(&rt)) && rt != nullptr)
        {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(rt->GetDesc(&d)))
                W = static_cast<float>(d.Width), H = static_cast<float>(d.Height);
            rt->Release();
        }
        if (W < 16 || H < 16)
        {
            g_front.clear();
            return;
        }
        struct Rhw
        {
            float x, y, z, rhw;
            D3DCOLOR color;
        };
        static std::vector<Rhw> out;
        out.clear();
        const auto& v = ctx_->camera.view;
        const auto& p = ctx_->camera.proj;
        auto project = [&](const Vertex& in, Rhw& o) -> bool {
            const float vx = in.x * v._11 + in.y * v._21 + in.z * v._31 + v._41;
            const float vy = in.x * v._12 + in.y * v._22 + in.z * v._32 + v._42;
            const float vz = in.x * v._13 + in.y * v._23 + in.z * v._33 + v._43;
            const float vw = in.x * v._14 + in.y * v._24 + in.z * v._34 + v._44;
            const float cx = vx * p._11 + vy * p._21 + vz * p._31 + vw * p._41;
            const float cy = vx * p._12 + vy * p._22 + vz * p._32 + vw * p._42;
            const float cw = vx * p._14 + vy * p._24 + vz * p._34 + vw * p._44;
            if (cw <= 0.01f)
                return false; // behind the camera
            const float nx = cx / cw, ny = cy / cw;
            if (std::abs(nx) > 8.0f || std::abs(ny) > 8.0f)
                return false;
            o = {(nx + 1.0f) * 0.5f * W - 0.5f, (1.0f - ny) * 0.5f * H - 0.5f, 0.0f, 1.0f, in.color};
            return true;
        };
        for (size_t i = 0; i + 2 < g_front.size(); i += 3)
        {
            Rhw t[3];
            if (project(g_front[i], t[0]) && project(g_front[i + 1], t[1]) && project(g_front[i + 2], t[2]))
                out.insert(out.end(), t, t + 3);
        }
        g_front.clear();
        if (out.empty())
            return;

        drawing_ = true;
        // Save exactly what we change.
        DWORD vs = 0, ps = 0;
        device_->GetVertexShader(&vs);
        device_->GetPixelShader(&ps);
        IDirect3DBaseTexture8* tex0 = nullptr;
        device_->GetTexture(0, &tex0);
        const D3DRENDERSTATETYPE rsKeys[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ALPHATESTENABLE,
            D3DRS_CULLMODE, D3DRS_LIGHTING, D3DRS_FOGENABLE, D3DRS_STENCILENABLE, D3DRS_COLORWRITEENABLE};
        DWORD rs[std::size(rsKeys)]{};
        for (size_t i = 0; i < std::size(rsKeys); ++i)
            device_->GetRenderState(rsKeys[i], &rs[i]);
        const D3DTEXTURESTAGESTATETYPE tsKeys[] = {D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1};
        DWORD ts0[std::size(tsKeys)]{}, ts1Color = 0, ts1Alpha = 0;
        for (size_t i = 0; i < std::size(tsKeys); ++i)
            device_->GetTextureStageState(0, tsKeys[i], &ts0[i]);
        device_->GetTextureStageState(1, D3DTSS_COLOROP, &ts1Color);
        device_->GetTextureStageState(1, D3DTSS_ALPHAOP, &ts1Alpha);

        device_->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        device_->SetPixelShader(0);
        device_->SetTexture(0, nullptr);
        device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device_->SetRenderState(D3DRS_LIGHTING, FALSE);
        device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
        constexpr size_t kChunk = 3 * 8000;
        for (size_t i = 0; i < out.size(); i += kChunk)
        {
            const size_t cnt = std::min(kChunk, out.size() - i);
            device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(cnt / 3), &out[i], sizeof(Rhw));
        }

        // Put it all back.
        for (size_t i = 0; i < std::size(rsKeys); ++i)
            device_->SetRenderState(rsKeys[i], rs[i]);
        for (size_t i = 0; i < std::size(tsKeys); ++i)
            device_->SetTextureStageState(0, tsKeys[i], ts0[i]);
        device_->SetTextureStageState(1, D3DTSS_COLOROP, ts1Color);
        device_->SetTextureStageState(1, D3DTSS_ALPHAOP, ts1Alpha);
        device_->SetTexture(0, tex0);
        if (tex0 != nullptr)
            tex0->Release();
        device_->SetPixelShader(ps);
        device_->SetVertexShader(vs);
        drawing_ = false;
    }

    void WorldRingRenderer::SetupStates(void)
    {
        const auto& cam = ctx_->camera;
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

    // Utsusemi shadows and Blink (incoming ember_dock_wiring WIRING.md U3, U7, K3, D2-D6), drawn in the 3D
    // scene. Every primitive is sorted into a BACK batch (depth-tested, so your character hides it) or a
    // FRONT batch (no depth test, always over your character) by which side of your body it is on, seen
    // from the camera; pieces near the sides go into both with complementary alpha. This works whether the
    // game draws your character before or after this pass. Sizes in px are 1080p screen pixels at the
    // player's distance, converted with pw (yalms per pixel).
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
        auto dot3  = [](V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        auto cross = [](V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        auto len   = [](V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); };
        auto norm  = [&](V3 a) { const float l = len(a); return l > 1e-5f ? mul(a, 1.0f / l) : V3{0, 0, 0}; };
        // Camera axes and position from the view matrix (rows of its rotation part).
        const V3 camR{v._11, v._21, v._31}, camU{v._12, v._22, v._32};
        V3 camF{v._13, v._23, v._33};
        const V3 camPos = mul(add(add(mul(camR, v._41), mul(camU, v._42)), mul(camF, v._43)), -1.0f);
        const V3 up{0, -1, 0}; // game world: up is -Y
        const V3 feet{sh.x, sh.y - 0.04f, sh.z};
        const V3 chest = add(feet, mul(up, 0.55f * sh.height));
        if (dot3(sub(chest, camPos), camF) < 0.0f) // forward must point from the camera towards you
            camF = mul(camF, -1.0f);
        const float projY = std::abs(ctx_->camera.proj._22) > 0.01f ? std::abs(ctx_->camera.proj._22) : 1.0f;
        const float pw    = 2.0f * len(sub(chest, camPos)) / (projY * 1080.0f);
        const float h     = sh.height * sh.size * sh.outScale;
        const float A     = std::clamp(sh.opacity * sh.outA, 0.0f, 1.0f);
        const Rgba hot{1.0f, 0.47f, 0.24f, 1}, ivory{0.95f, 0.91f, 0.85f, 1}, grey{0.55f, 0.58f, 0.66f, 1}, dark{0.06f, 0.07f, 0.11f, 1};
        const Rgba& col = sh.color;
        const int n     = std::max(1, sh.maxShown);

        // --- batches (FRONT is kept for DrawFront, after the scene)
        static std::vector<Vertex> back;
        std::vector<Vertex>& front = g_front;
        back.clear(), front.clear();
        g_frontFrame = ctx_->camera.frame;
        constexpr float kOverlay = -1.0f; // ground / overhead pieces: on top in split mode (D4)
        // Back weight for a point: 0 = in front of your body (towards the camera), 1 = behind it. The
        // anchor is the body axis at the point's own height, never the feet (D2).
        auto backWeight = [&](V3 p, float radius) -> float {
            if (sh.depthMode == 0)
                return 0.0f;
            if (sh.depthMode == 2)
                return 1.0f;
            if (radius < 0.0f)
                return 0.0f;
            const V3 axis{feet.x, p.y, feet.z};
            const float side = dot3(sub(p, axis), camF);
            const float w    = std::max(0.02f, sh.crossfade * std::max(radius, 0.1f));
            const float t    = std::clamp((side + w) / (2.0f * w), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        auto scaled = [](D3DCOLOR c, float k) -> D3DCOLOR {
            const DWORD a = static_cast<DWORD>(std::clamp(static_cast<float>(c >> 24) * k, 0.0f, 255.0f) + 0.5f);
            return (c & 0x00FFFFFFu) | (a << 24);
        };
        auto put = [&](const Vertex& a, const Vertex& b, const Vertex& c, float bw) {
            if (bw > 0.001f)
                back.push_back({a.x, a.y, a.z, scaled(a.color, bw)}), back.push_back({b.x, b.y, b.z, scaled(b.color, bw)}),
                    back.push_back({c.x, c.y, c.z, scaled(c.color, bw)});
            if (bw < 0.999f)
                front.push_back({a.x, a.y, a.z, scaled(a.color, 1.0f - bw)}), front.push_back({b.x, b.y, b.z, scaled(b.color, 1.0f - bw)}),
                    front.push_back({c.x, c.y, c.z, scaled(c.color, 1.0f - bw)});
        };
        auto vx = [](V3 p, D3DCOLOR c) { return Vertex{p.x, p.y, p.z, c}; };

        // --- primitives (radius: orbit radius for the front/back crossfade, or kOverlay)
        auto tri = [&](V3 a, V3 b, V3 c, D3DCOLOR k, float radius) {
            put(vx(a, k), vx(b, k), vx(c, k), backWeight(mul(add(add(a, b), c), 1.0f / 3.0f), radius));
        };
        // Camera-facing line of `wPx` screen pixels.
        auto line = [&](V3 a, V3 b, float wPx, D3DCOLOR k, float radius) {
            const V3 mid = mul(add(a, b), 0.5f);
            const V3 o   = mul(norm(cross(sub(b, a), sub(mid, camPos))), wPx * pw * 0.5f);
            const float bw = backWeight(mid, radius);
            put(vx(add(a, o), k), vx(sub(a, o), k), vx(add(b, o), k), bw);
            put(vx(sub(a, o), k), vx(sub(b, o), k), vx(add(b, o), k), bw);
        };
        auto glowLine = [&](V3 a, V3 b, const Rgba& c, float a0, float radius) {
            if (sh.glow)
                line(a, b, 4.0f, Argb(c, a0 * 0.18f), radius);
            line(a, b, 1.5f, Argb(c, a0), radius);
        };
        // Camera-facing soft dot: centre colour fading to the rim colour (radius r in world units).
        auto dot = [&](V3 p, float r, D3DCOLOR cIn, D3DCOLOR cOut, float radius, int seg = 12) {
            if ((cIn >> 24) == 0 && (cOut >> 24) == 0)
                return;
            const float bw = backWeight(p, radius);
            V3 prev = add(p, mul(camR, r));
            for (int i = 1; i <= seg; ++i)
            {
                const float a = static_cast<float>(i) * kTwoPi / static_cast<float>(seg);
                const V3 cur  = add(p, add(mul(camR, std::cos(a) * r), mul(camU, std::sin(a) * r)));
                put(vx(p, cIn), vx(prev, cOut), vx(cur, cOut), bw);
                prev = cur;
            }
        };
        // Diamond: camera-facing (ground = false) or flat on the ground (ground = true), half size r.
        auto diamond = [&](V3 p, float r, D3DCOLOR k, bool ground) {
            const V3 ax = ground ? V3{r, 0, 0} : mul(camR, r), ay = ground ? V3{0, 0, r} : mul(camU, r);
            tri(add(p, ay), add(p, ax), sub(p, ay), k, kOverlay);
            tri(add(p, ay), sub(p, ax), sub(p, ay), k, kOverlay);
        };
        // Horizontal ring (width in px) around the body axis at centre c; ground rings use kOverlay.
        auto ring = [&](V3 c, float R, float wPx, D3DCOLOR cIn, D3DCOLOR cOut, float radius) {
            constexpr int kSeg = 64;
            const float hw = wPx * pw * 0.5f;
            for (int i = 0; i < kSeg; ++i)
            {
                const float a0 = kTwoPi * static_cast<float>(i) / kSeg, a1 = kTwoPi * static_cast<float>(i + 1) / kSeg;
                const V3 i0 = {c.x + std::cos(a0) * (R - hw), c.y, c.z + std::sin(a0) * (R - hw)}, o0 = {c.x + std::cos(a0) * (R + hw), c.y, c.z + std::sin(a0) * (R + hw)};
                const V3 i1 = {c.x + std::cos(a1) * (R - hw), c.y, c.z + std::sin(a1) * (R - hw)}, o1 = {c.x + std::cos(a1) * (R + hw), c.y, c.z + std::sin(a1) * (R + hw)};
                const float bw = backWeight(mul(add(i0, o1), 0.5f), radius);
                put(vx(i0, cIn), vx(o0, cOut), vx(o1, cOut), bw);
                put(vx(i0, cIn), vx(o1, cOut), vx(i1, cIn), bw);
            }
        };
        // Flat soft disc on the ground (smoke pool).
        auto pool = [&](V3 c, float R, D3DCOLOR k) {
            constexpr int kSeg = 24;
            for (int i = 0; i < kSeg; ++i)
            {
                const float a0 = kTwoPi * static_cast<float>(i) / kSeg, a1 = kTwoPi * static_cast<float>(i + 1) / kSeg;
                tri(c, {c.x + std::cos(a0) * R, c.y, c.z + std::sin(a0) * R}, {c.x + std::cos(a1) * R, c.y, c.z + std::sin(a1) * R}, k, kOverlay);
            }
        };
        // Per-slot state: lit, the one just lost (flash), or empty.
        auto lit  = [&](int k) { return k < sh.count; };
        auto lost = [&](int k) { return k == sh.lostIdx && sh.lostA > 0.0f; };
        const float litA = A * sh.inA;

        // --- K4 order: ground, body volume (Blink), orbiters, overhead
        if (sh.style == 3) // rune ring: a ground circle with one rune per shadow
        {
            const float R = 0.62f * h, rr = 0.09f * h;
            ring(feet, R, 2.0f, Argb(col, 0.55f * A), Argb(col, 0.55f * A), kOverlay);
            if (sh.glow)
                ring(feet, R + 3.5f * pw, 5.0f, Argb(col, 0.15f * A), Argb(col, 0.0f), kOverlay);
            for (int k = 0; k < n; ++k)
            {
                const float a = -kPi * 0.5f + kTwoPi * static_cast<float>(k) / static_cast<float>(n);
                const V3 p    = {feet.x + std::cos(a) * R, feet.y - 0.01f, feet.z + std::sin(a) * R};
                if (lost(k))
                {
                    diamond(p, rr * (1.0f + 0.8f * (1.0f - sh.lostA)), Argb(hot, sh.lostA * A), true);
                    continue;
                }
                diamond(p, rr * 1.2f, Argb(lit(k) ? col : grey, 0.35f * (lit(k) ? litA : A)), true);
                diamond(p, rr, Argb(lit(k) ? col : dark, lit(k) ? litA : 0.7f * A), true);
                if (lit(k))
                    diamond(p, rr * 0.4f, Argb(ivory, litA), true);
            }
        }

        if (sh.blinkA > 0.0f) // Blink shimmer: motes in a body-shaped volume, a faint veil
        {
            const float bh = sh.height * sh.blinkSize, R = 0.34f * bh, k = sh.blinkDissipate;
            const float ripple = sh.blinkRipple >= 0.0f ? 1.0f - sh.blinkRipple : 0.0f; // 1 at the hit -> 0
            const float BA = sh.blinkA;
            // Veil (fades first when dissipating).
            const float veil = sh.veilAlpha * BA * (1.0f - std::min(1.0f, k / 0.375f)) * (1.0f - sh.blinkCondense);
            for (int i = 0; i < 3; ++i)
                dot(add(feet, mul(up, (0.25f + 0.25f * static_cast<float>(i)) * bh)), 0.42f * bh, Argb(sh.blinkColor, veil), Argb(sh.blinkColor, 0.0f), R, 16);
            for (int i = 0; i < sh.blinkMotes; ++i)
            {
                // Fixed pseudo-random body-space offsets per mote.
                auto hash = [](uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return static_cast<float>(x & 0xFFFFFF) / 16777215.0f; };
                const auto ui = static_cast<uint32_t>(i);
                const float hv = hash(ui * 3 + 1), phi0 = hash(ui * 3 + 2) * kTwoPi, wr = 0.55f + 0.45f * hash(ui * 3 + 3), rate = 1.5f + 3.0f * hash(ui * 7 + 5);
                float r = R * std::sin(kPi * (0.15f + 0.85f * hv)) * wr;
                r *= 1.0f + 0.8f * sh.blinkCondense; // cast: start further out
                r *= 1.0f + 0.5f * k;                // wear-off: spread
                r *= 1.0f + 0.35f * ripple;          // hit: pushed outward
                const float phi = phi0 + sh.blinkTime * 0.4f;
                V3 p = add(feet, add(mul(up, hv * bh + 0.6f * bh * k), V3{std::cos(phi) * r, 0, std::sin(phi) * r}));
                float a = (0.5f + 0.5f * std::sin(sh.blinkTime * rate + phi0)) * BA * (1.0f - k) * (1.0f - sh.blinkCondense * 0.9f);
                a = std::min(1.0f, a * (1.0f + 0.6f * ripple));
                const bool big = (i % 10) == 0;
                const float ms = (big ? 3.0f : 1.8f) * pw * sh.blinkSize;
                dot(p, ms, Argb(big ? sh.blinkAccent : sh.blinkColor, a), Argb(sh.blinkColor, 0.0f), R, 6);
                if (big) // 4-point star
                {
                    const float s = ms * 2.6f;
                    line(sub(p, mul(camR, s)), add(p, mul(camR, s)), 1.0f, Argb(sh.blinkAccent, a * 0.7f), R);
                    line(sub(p, mul(camU, s)), add(p, mul(camU, s)), 1.0f, Argb(sh.blinkAccent, a * 0.7f), R);
                }
            }
            if (sh.blinkFlash > 0.0f)
                dot(chest, 0.6f * bh, Argb(sh.blinkAccent, 0.25f * sh.blinkFlash * BA), Argb(sh.blinkAccent, 0.0f), kOverlay, 20);
            if (ripple > 0.0f) // one faint ivory ellipse at the chest
                ring(chest, R * (1.0f + 0.6f * (1.0f - ripple)), 1.5f, Argb(sh.blinkAccent, 0.35f * ripple * BA), Argb(sh.blinkAccent, 0.35f * ripple * BA), R);
        }

        switch (sh.style)
        {
        case 1: // wisps: orbiting motes with a fading trail, a faint ground ring
        {
            const float R = 0.5f * h;
            ring(feet, R, 3.0f, Argb(col, 0.43f * A), Argb(col, 0.43f * A), kOverlay);
            for (int k = 0; k < n; ++k)
            {
                const float phi = sh.time + kTwoPi * static_cast<float>(k) / static_cast<float>(n);
                auto at = [&](float a, float r) { return add(chest, V3{std::cos(a) * r, 0, std::sin(a) * r}); };
                if (lost(k))
                {
                    dot(at(phi, R * (1.0f + 0.6f * (1.0f - sh.lostA))), 9.0f * pw * sh.size, Argb(hot, sh.lostA * A), Argb(hot, 0.0f), R);
                    continue;
                }
                if (!lit(k))
                {
                    dot(at(phi, R), 2.5f * pw, Argb(grey, 0.3f * A), Argb(grey, 0.0f), R);
                    continue;
                }
                for (int j = 6; j >= 1; --j) // trail along the past angles
                    dot(at(phi - 0.12f * static_cast<float>(j), R), 2.0f * pw * sh.size, Argb(col, litA * 0.5f * (1.0f - j / 7.0f)), Argb(col, 0.0f), R, 8);
                const V3 p = at(phi, R);
                if (sh.glow) // soft halo in the wisp's own colour (no dark ink ring: it read as a black circle on bright ground)
                {
                    dot(p, 16.0f * pw * sh.size, Argb(col, 0.16f * litA), Argb(col, 0.0f), R, 16);
                    dot(p, 9.0f * pw * sh.size, Argb(col, 0.35f * litA), Argb(col, 0.0f), R, 16);
                }
                dot(p, 5.5f * pw * sh.size, Argb(col, litA), Argb(col, 0.0f), R);
                dot(p, 2.5f * pw * sh.size, Argb(ivory, litA), Argb(ivory, 0.3f * litA), R);
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
            const Rgba amber{0.90f, 0.71f, 0.47f, 1};
            for (int k = 0; k < n; ++k) // faint fills, classified by facet centroid
            {
                const V3 a = e[static_cast<size_t>(k)], b = e[static_cast<size_t>((k + 1) % n)];
                if (lit(k) || lost(k))
                {
                    const Rgba& c  = lost(k) ? hot : col;
                    const float fa = lost(k) ? 0.45f * sh.lostA * A : 0.06f * litA;
                    tri(top, a, b, Argb(c, fa), R);
                    tri(bot, a, b, Argb(c, fa), R);
                }
            }
            for (int k = 0; k < n; ++k)
            {
                const int kp     = (k + n - 1) % n;
                const V3 a       = e[static_cast<size_t>(k)], b = e[static_cast<size_t>((k + 1) % n)];
                const bool on    = lit(k), onSide = lit(k) || lit(kp);
                const bool flash = lost(k) || lost(kp);
                if (lost(k))
                    glowLine(a, b, hot, sh.lostA * A, R);
                else if (on)
                    glowLine(a, b, amber, 0.9f * litA, R);
                else
                    line(a, b, 1.0f, Argb(grey, 0.27f * A), R);
                const Rgba& sc = flash ? hot : amber;
                const float sa = flash ? sh.lostA * A : onSide ? 0.9f * litA : 0.0f;
                if (sa > 0.0f)
                    glowLine(top, a, sc, sa, R), glowLine(bot, a, sc, sa, R);
                else
                    line(top, a, 1.0f, Argb(grey, 0.27f * A), R), line(bot, a, 1.0f, Argb(grey, 0.27f * A), R);
            }
            dot(top, 3.0f * pw, Argb(ivory, A), Argb(ivory, 0.4f * A), kOverlay);
            dot(bot, 3.0f * pw, Argb(ivory, A), Argb(ivory, 0.4f * A), R);
            break;
        }
        case 4: // smoke: soft clone columns beside the player, a lost clone disperses
        {
            const float offs[6] = {-0.55f, 0.55f, -1.0f, 1.0f, -1.45f, 1.45f};
            const V3 side = norm(V3{camR.x, 0, camR.z}); // sideways relative to the camera, on the ground
            for (int k = 0; k < std::min(n, 6); ++k)
            {
                const bool gone = lost(k);
                if (!lit(k) && !gone)
                    continue;
                const float spread = gone ? 1.0f + 1.5f * (1.0f - sh.lostA) : 1.0f;
                const float ka     = gone ? sh.lostA * A * 0.8f : litA;
                const V3 base      = add(feet, mul(side, offs[k] * h));
                pool(base, 0.22f * h * spread, Argb(dark, 0.25f * ka));
                constexpr int kPuffs = 14;
                for (int i = 0; i < kPuffs; ++i)
                {
                    const float t  = static_cast<float>(i) / static_cast<float>(kPuffs - 1);
                    const float ph = sh.time * 2.0f + static_cast<float>(i * 7 + k * 13);
                    const V3 jit{std::sin(ph) * 0.05f * h * spread, 0, std::cos(ph * 1.3f) * 0.05f * h * spread};
                    const float r = (0.13f + 0.05f * std::sin(ph * 0.7f)) * h * spread;
                    // The clones stand beside you: classify against your body like the other orbiters.
                    dot(add(add(base, mul(up, (0.08f + 0.8f * t) * h)), jit), r, Argb(col, 0.3f * ka), Argb(col, 0.0f), std::abs(offs[k]) * h);
                }
                dot(add(base, mul(up, 0.95f * h)), 0.12f * h * spread, Argb(dark, 0.45f * ka), Argb(dark, 0.0f), std::abs(offs[k]) * h); // head
            }
            break;
        }
        case 5: // pips: a row of small diamonds above the head
        {
            const V3 anchor  = add(feet, mul(up, 1.18f * sh.height));
            const float step = 24.0f * pw * sh.size, r = 7.0f * pw * sh.size;
            for (int k = 0; k < n; ++k)
            {
                const V3 p = add(anchor, mul(camR, (static_cast<float>(k) - static_cast<float>(n - 1) * 0.5f) * step));
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

        // --- BACK pass (depth-tested against the scene), then FRONT pass (over everything)
        auto flush = [&](const std::vector<Vertex>& vs) {
            constexpr size_t kChunk = 3 * 8000;
            for (size_t i = 0; i < vs.size(); i += kChunk)
            {
                const size_t cnt = std::min(kChunk, vs.size() - i);
                device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(cnt / 3), &vs[i], sizeof(Vertex));
            }
        };
        device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
        flush(back);
        // The front batch waits for DrawFront: the game may still draw your character after this pass.
    }

    // Cast ring: a dark track on the ground around your feet, the progress arc filling clockwise (as seen on
    // screen) from the direction you face (so it starts and finishes there), a bright head at its end; finished = the full ring
    // flashing out. Widths in 1080p screen px, converted at your distance like the target ring.
    void WorldRingRenderer::DrawCast(void)
    {
        const auto& c  = ctx_->cast;
        const auto& v  = ctx_->camera.view;
        const float cx = c.x, cy = c.y - 0.04f, cz = c.z;
        // Camera position and right vector from the view matrix.
        const float rx = v._11, ry = v._21, rz = v._31;
        const float px = -(v._41 * v._11 + v._42 * v._12 + v._43 * v._13);
        const float py = -(v._41 * v._21 + v._42 * v._22 + v._43 * v._23);
        const float pz = -(v._41 * v._31 + v._42 * v._32 + v._43 * v._33);
        (void)ry;
        const float dx = cx - px, dy = cy - py, dz = cz - pz;
        const float dist  = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float projY = std::abs(ctx_->camera.proj._22) > 0.01f ? std::abs(ctx_->camera.proj._22) : 1.0f;
        const float pw    = 2.0f * dist / (projY * 1080.0f);
        const float r     = std::max(0.3f, c.radius) * (1.0f + 0.25f * c.grow);
        const float A     = std::clamp(c.alpha, 0.0f, 1.0f);
        // Clockwise on screen: at the point away from the camera a clockwise sweep moves towards screen-right
        // (that decides the direction for the whole circle). It starts where you face (same angle as the
        // target ring's chevron: -yaw in ring space).
        const float aFar = std::atan2(cz - pz, cx - px);
        const float dir  = (-std::sin(aFar) * rx + std::cos(aFar) * rz) >= 0.0f ? 1.0f : -1.0f;
        const float a0   = -c.yaw;
        const float span = kTwoPi * std::clamp(c.progress, 0.0f, 1.0f);
        auto arc = [&](float rIn, float rOut, float from, float len, D3DCOLOR cIn, D3DCOLOR cOut) {
            if (len <= 0.0f)
                return;
            if (dir > 0)
                Strip(cx, cy, cz, rIn, rOut, from, from + len, cIn, cOut);
            else
                Strip(cx, cy, cz, rIn, rOut, from - len, from, cIn, cOut);
        };
        const Rgba track{0.04f, 0.05f, 0.09f, 1.0f}, ivory{0.95f, 0.91f, 0.85f, 1.0f};
        const float tw = 4.0f * pw, lw = 2.5f * pw;
        if (!c.flash)
            Strip(cx, cy, cz, r - tw, r + tw, 0.0f, kTwoPi, Argb(track, 0.55f * A), Argb(track, 0.55f * A));
        // glow, then the solid arc
        for (int i = 3; i >= 1; --i)
        {
            const float hw = lw + static_cast<float>(i) * 2.5f * pw;
            const float ga = A * 0.16f / static_cast<float>(i);
            arc(std::max(0.0f, r - hw), r, a0, span, Argb(c.color, 0.0f), Argb(c.color, ga));
            arc(r, r + hw, a0, span, Argb(c.color, ga), Argb(c.color, 0.0f));
        }
        arc(r - lw, r + lw, a0, span, Argb(c.color, A), Argb(c.color, A));
        arc(r * 0.995f - 0.5f * pw, r * 0.995f + 0.5f * pw, a0, span, Argb(ivory, 0.45f * A), Argb(ivory, 0.45f * A));
        if (!c.flash && span > 0.0f) // bright head at the leading edge
        {
            const float ea = a0 + dir * span;
            Disk(cx + std::cos(ea) * r, cy, cz + std::sin(ea) * r, 4.5f * pw, Argb(ivory, A));
            Disk(cx + std::cos(ea) * r, cy, cz + std::sin(ea) * r, 9.0f * pw, Argb(c.color, 0.25f * A));
        }
        // faint fill inside
        Disk(cx, cy, cz, r, Argb(c.color, (c.flash ? 0.10f : 0.05f) * A));
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
