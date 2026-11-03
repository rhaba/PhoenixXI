/**
 * Farplane UI - Copyright (c) 2026
 *
 * A holistic refined/ornate UI replacement for Final Fantasy XI (Ashita v4).
 *
 * Based on the official Ashita example plugin template.
 */

#ifndef FARPLANEUI_HPP_INCLUDED
#define FARPLANEUI_HPP_INCLUDED

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

#include "Ashita.h"
#include "mask.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "worldring.hpp"

#include "modules.hpp"

#include <memory>
#include <string>
#include <vector>

namespace farplaneui
{
    class plugin final : public IPlugin
    {
        IAshitaCore* core_;
        ILogManager* log_;
        IDirect3DDevice8* device_;
        WorldRingRenderer worldRing_;
        uint32_t id_;

        // Modules (order = render order; the mask is first and also gets the D3D draw hooks).
        MaskModule* mask_{nullptr};
        ChatModule* chat_{nullptr};
        std::vector<std::unique_ptr<IModule>> modules_;
        Context ctx_{};
        TextureCache textures_{};
        float chatWidth_{0.38f}; // [layout] chat width as a fraction of the screen

        bool show_settings_window_{false};
        int settingsModule_{0}, settingsSection_{0};
        bool reported_{false};
        bool active_{false}, gateNotified_{false}; // linkshell gate (lsgate.hpp)
        double gateCheckedAt_{0};
        bool d3dInit_{false};
        bool dirty_{false};
        bool cameraThisFrame_{false};
        // /fpui perf [minutes]: time spent per module (Frame + Render, text, packets) and in the draw-call
        // hook, plus the gap between frames (stutter), written to <Ashita>/logs every 5 seconds.
        struct Perf
        {
            bool on{false};
            std::string file;
            double sessionStart{0}, sessionLen{300}, lastPresent{0};
            // current 5 s window
            double startedAt{0}, frames{0}, present{0}, presentMax{0}, draw{0}, drawCalls{0}, gapMax{0}, drawMask{0}, drawCamera{0};
            int hitches{0};
            std::vector<double> frame, render, text, packet; // seconds, by module index
            std::vector<double> cur, peak;                   // this frame / worst frame (Frame + Render), by module
            // whole session
            double totalFrames{0}, totalPresent{0}, worstGap{0}, worstOurs{0};
            int totalHitches{0}, windows{0};
            std::vector<double> totalModule, worstModule;
        } perf_;
        static auto Qpc(void) -> double;
        void PerfStart(double minutes);
        void PerfWindow(void);
        void PerfFinish(const char* why);
        void PerfWrite(const std::string& text);
        void ComputeEmberLayout(void);
        void RenderPerfSettings(IGuiManager* gui);
        float perfMinutes_{5.0f};
        std::string perfLastWindow_, perfLastSummary_; // shown on the Performance settings page
        double dirty_at_{0.0};

    public:
        plugin(void);
        ~plugin(void) override;

        auto GetName(void) const -> const char* override;
        auto GetAuthor(void) const -> const char* override;
        auto GetDescription(void) const -> const char* override;
        auto GetLink(void) const -> const char* override;
        auto GetVersion(void) const -> double override;
        auto GetInterfaceVersion(void) const -> double override;
        auto GetPriority(void) const -> int32_t override;
        auto GetFlags(void) const -> uint32_t override;

        auto Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool override;
        auto Release(void) -> void override;

        auto HandleEvent(const char* eventName, const void* eventData, const uint32_t eventSize) -> void override;
        auto HandleCommand(int32_t mode, const char* command, bool injected) -> bool override;
        auto HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modifiedMode, bool* modifiedIndent, char* modifiedMessage, bool injected, bool blocked) -> bool override;
        auto HandleOutgoingText(int32_t mode, const char* message, int32_t* modifiedMode, char* modifiedMessage, bool injected, bool blocked) -> bool override;
        auto HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool override;
        auto HandleOutgoingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t sizeChunk, const uint8_t* dataChunk, bool injected, bool blocked) -> bool override;

        auto Direct3DInitialize(IDirect3DDevice8* device) -> bool override;
        auto Direct3DBeginScene(bool isRenderingBackBuffer) -> void override;
        auto Direct3DEndScene(bool isRenderingBackBuffer) -> void override;
        auto Direct3DPresent(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) -> void override;
        auto Direct3DSetRenderState(D3DRENDERSTATETYPE State, DWORD* Value) -> bool override;
        auto Direct3DDrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) -> bool override;
        auto Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) -> bool override;
        auto Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool override;
        auto Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) -> bool override;

        // Host services for modules (Context callbacks).
        void MarkDirty(void);
        void Print(const std::string& text) const;

    private:
        void LoadAll(void);
        void SaveAll(void);
        void ComputeLayout(void);
        auto InCutscene(void) const -> bool;
        void ReportStatus(void);
        void RenderSettingsWindow(IGuiManager* gui);
        void OnDraw(void);
        auto TryCaptureCamera(int source) -> bool;
    };

} // namespace farplaneui

#endif // FARPLANEUI_HPP_INCLUDED
