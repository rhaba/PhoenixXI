/**
 * Farplane UI - target ground ring drawn inside the 3D scene.
 *
 * The game renders the world into an offscreen target, then switches render targets to composite it
 * and draw its UI. Hooking IDirect3DDevice8::SetRenderTarget (a vtable slot patch on the device Ashita
 * hands us) gives the moment the world is finished while its depth buffer is still bound: the ring is
 * drawn there, so the mob and terrain occlude it like real geometry.
 */

#ifndef FARPLANEUI_WORLDRING_HPP_INCLUDED
#define FARPLANEUI_WORLDRING_HPP_INCLUDED

#include "module.hpp"

namespace farplaneui
{
    class WorldRingRenderer
    {
    public:
        // Patches SetRenderTarget on the device's vtable. `ctx` must outlive Uninstall().
        auto Install(IDirect3DDevice8* device, Context* ctx) -> bool;
        void Uninstall(void);
        // True while our own draw is running (the host skips its draw-call hooks).
        auto Drawing(void) const -> bool { return drawing_; }

    private:
        static auto __stdcall SetRenderTargetHook(IDirect3DDevice8* self, IDirect3DSurface8* target, IDirect3DSurface8* depth) -> HRESULT;
        void OnSetRenderTarget(IDirect3DDevice8* self, IDirect3DSurface8* target);
        void Draw(void);
        void DrawRing(void);
        void DrawShadows(void); // Utsusemi shadows around the player (ctx.shadows)
        void Strip(float cx, float cy, float cz, float rIn, float rOut, float b0, float b1, D3DCOLOR cIn, D3DCOLOR cOut);
        void Disk(float cx, float cy, float cz, float rad, D3DCOLOR c);

        IDirect3DDevice8* device_{nullptr};
        Context* ctx_{nullptr};
        DWORD stateBlock_{0};
        bool drawing_{false};
    };
} // namespace farplaneui

#endif // FARPLANEUI_WORLDRING_HPP_INCLUDED
