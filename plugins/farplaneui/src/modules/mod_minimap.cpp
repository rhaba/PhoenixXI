/**
 * Farplane UI - minimap module: the game's own zone map, centred on the player.
 *
 * Method is the one used by atom0s's Minimap plugin (closed source; its debug strings list the map
 * table fields ZoneId/FloorId/FloorIndex/Flags/Scale/KeyItemOffset/MapDatOffset/OffsetX/OffsetY,
 * and it ships the same FFXiMain.dll signatures) and documented in source by the open Ashita v4
 * addon boussole (loonsies, https://github.com/loonsies/boussole, src/map.lua + src/texture.lua):
 *   map table : sig "8A0D????????5333C05684C95774??8A5424188B7424148B7C2410B9", table = *(sig+0x1C),
 *               0x0E-byte entries: u16 zone, u8 floorId, u8 floorIndex, u8 flags, s8 scale,
 *               s8 keyItemOffset, u8 ?, u16 mapDatOffset, s16 offsetX, s16 offsetY
 *   floor     : int __thiscall GetFloor(g_pTsZoneMap, X, Z, Y)  (Ashita axes; Y/Z swapped)
 *               fn sig "8B542408568D4424108BF18B4C2410508B44240C",
 *               this = **(sig "8B7424148B4424108B7C240C8B0D" + 0x0E)
 *   DAT id    : flags & 0x0F: 0 -> 5312+off, 1 -> 53295+off, 2 -> 54295+off; path via
 *               IResourceManager::GetFilePath (XiPivot-aware because XiPivot hooks file access)
 *   image     : 44-byte header at DAT+0x41 (width @+4, height @+8, bitCount @+0x0E, type @+0x28);
 *               type 0x0A = bitmap (8-bit: 256 BGRA palette then indices; bottom-up rows),
 *               'DXT1'..'DXT5' = compressed blocks after 8 extra bytes
 *   position  : mapX = X*|scale|/5, mapY = -Y*|scale|/5 (512-unit map space),
 *               texel = (map - offset) * texSize/512
 * Load order (like Minimap): resources\farplaneui\maps\<zone>_<floor>.png override (same
 * calibration as the DAT map), then the game DAT, then the legacy <zone>.png with ini calibration.
 */
#include "common.hpp"
#include "../ember.hpp"

#include <cmath>
#include <cstdio>

namespace farplaneui
{
    namespace
    {
        constexpr const char* kTableSig   = "8A0D????????5333C05684C95774??8A5424188B7424148B7C2410B9";
        constexpr const char* kFloorFnSig = "8B542408568D4424108BF18B4C2410508B44240C";
        constexpr const char* kFloorThis  = "8B7424148B4424108B7C240C8B0D";
        constexpr uint32_t kEntrySize     = 0x0E;
        constexpr uint32_t kMaxEntries    = 3000;

        using FloorFn = int32_t(__thiscall*)(void* self, float x, float y, float z);

        auto Readable(uintptr_t addr, size_t n) -> bool
        {
            if (addr == 0)
                return false;
            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
                return false;
            if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
                return false;
            return addr + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        }
        auto RdU32(uintptr_t a) -> uintptr_t { return Readable(a, 4) ? *reinterpret_cast<const uint32_t*>(a) : 0u; }

        auto CallFloor(FloorFn fn, void* self, float x, float y, float z) -> int32_t
        {
#ifdef _MSC_VER
            __try
            {
                return fn(self, x, y, z);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
#else
            return fn(self, x, y, z);
#endif
        }

        auto U16(const std::vector<uint8_t>& d, size_t o) -> uint32_t { return static_cast<uint32_t>(d[o] | (d[o + 1] << 8)); }
        auto U32(const std::vector<uint8_t>& d, size_t o) -> uint32_t
        {
            return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) | (static_cast<uint32_t>(d[o + 2]) << 16) |
                   (static_cast<uint32_t>(d[o + 3]) << 24);
        }
    } // namespace

    void MinimapModule::Scan(Context& ctx)
    {
        scanned_        = true;
        const HMODULE m = ::GetModuleHandleA("FFXiMain.dll");
        if (m == nullptr)
            return;
        const auto base = reinterpret_cast<uintptr_t>(m);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + static_cast<uintptr_t>(dos->e_lfanew));
        const uintptr_t size = nt->OptionalHeader.SizeOfImage;
        const uintptr_t t    = Ashita::Memory::FindPattern(base, size, kTableSig, 0, 0);
        table_               = t != 0 ? RdU32(t + 0x1C) : 0;
        floorFn_             = Ashita::Memory::FindPattern(base, size, kFloorFnSig, 0, 0);
        floorThis_           = Ashita::Memory::FindPattern(base, size, kFloorThis, 0x0E, 0);
        if (table_ == 0)
            ctx.Print("Minimap: map table signature not found (client version?); only PNG maps can be shown.");
        if (floorFn_ == 0 || floorThis_ == 0)
            ctx.Print("Minimap: floor function signatures not found; multi-floor zones use their first map.");
    }

    auto MinimapModule::ReadEntry(uint32_t i, MapEntry& e) const -> bool
    {
        const uintptr_t b = table_ + i * kEntrySize;
        if (table_ == 0 || !Readable(b, kEntrySize))
            return false;
        const auto* p = reinterpret_cast<const uint8_t*>(b);
        e.zone       = static_cast<uint16_t>(p[0] | (p[1] << 8));
        e.floorId    = p[2];
        e.floorIndex = p[3];
        e.flags      = p[4];
        e.scale      = static_cast<int8_t>(p[5]);
        e.keyOffset  = static_cast<int8_t>(p[6]);
        e.datOffset  = static_cast<uint16_t>(p[8] | (p[9] << 8));
        e.offsetX    = static_cast<int16_t>(p[10] | (p[11] << 8));
        e.offsetY    = static_cast<int16_t>(p[12] | (p[13] << 8));
        return true;
    }

    auto MinimapModule::CurrentFloor(float x, float y, float z) const -> int32_t
    {
        if (floorFn_ == 0 || floorThis_ == 0)
            return -1;
        const uintptr_t self = RdU32(RdU32(floorThis_));
        if (self == 0)
            return -1;
        return CallFloor(reinterpret_cast<FloorFn>(floorFn_), reinterpret_cast<void*>(self), x, z, y);
    }

    void MinimapModule::FreeMap(void)
    {
        if (datTex_ != nullptr)
            datTex_->Release();
        datTex_ = nullptr;
        tex_    = nullptr;
        texW_ = texH_ = 0;
    }

    auto MinimapModule::LoadDat(Context& ctx, uint32_t fileId) -> bool
    {
        auto* rm = ctx.core->GetResourceManager();
        char path[MAX_PATH]{};
        if (rm == nullptr || ctx.device == nullptr)
            return false;
        rm->GetFilePath(fileId, path, sizeof(path));
        if (path[0] == '\0')
        {
            status_ = "no DAT path for file id " + std::to_string(fileId);
            return false;
        }
        FILE* f = nullptr;
        if (::fopen_s(&f, path, "rb") != 0 || f == nullptr)
        {
            status_ = std::string("cannot open ") + path;
            return false;
        }
        std::vector<uint8_t> d;
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0 && n < 64L * 1024L * 1024L)
        {
            d.resize(static_cast<size_t>(n));
            d.resize(std::fread(d.data(), 1, d.size(), f));
        }
        std::fclose(f);
        constexpr size_t H = 0x41, HS = 0x2C; // header size 44 (boussole ImageHeader)
        if (d.size() < H + HS)
        {
            status_ = "DAT too small";
            return false;
        }
        const auto w = static_cast<int32_t>(U32(d, H + 4)), h = static_cast<int32_t>(U32(d, H + 8));
        const uint32_t bits = U16(d, H + 0x0E), type = U32(d, H + 0x28);
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        {
            status_ = "bad DAT image size";
            return false;
        }
        const auto uw = static_cast<UINT>(w), uh = static_cast<UINT>(h);
        IDirect3DTexture8* t = nullptr;
        D3DLOCKED_RECT lr{};
        if (type == 0x0000000Au) // bitmap
        {
            const size_t px = static_cast<size_t>(uw) * uh;
            const size_t bpp = bits / 8u;
            const size_t need = H + HS + (bits == 8 ? 1024 + px : px * bpp);
            if (bpp == 0 || bpp > 4 || d.size() < need ||
                FAILED(ctx.device->CreateTexture(uw, uh, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t)) || t == nullptr)
            {
                status_ = "bitmap decode failed";
                return false;
            }
            if (FAILED(t->LockRect(0, &lr, nullptr, 0)))
            {
                t->Release();
                return false;
            }
            const size_t pal = H + HS;
            size_t o         = bits == 8 ? pal + 1024 : pal;
            for (size_t i = 0; i < px; ++i)
            {
                uint8_t b = 0, g = 0, r = 0, a = 255;
                const uint8_t* s = d.data() + (bits == 8 ? pal + static_cast<size_t>(d[o]) * 4 : o);
                if (bits == 8 || bits == 32)
                {
                    b = s[0], g = s[1], r = s[2], a = static_cast<uint8_t>(s[3] > 0 ? 255 : 0);
                }
                else if (bits == 24)
                {
                    b = s[0], g = s[1], r = s[2];
                }
                else if (bits == 16)
                {
                    const uint32_t c = static_cast<uint32_t>(s[0] | (s[1] << 8));
                    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
                    g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
                    b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
                }
                o += bits == 8 ? 1 : bpp;
                const size_t x = i % uw, y = uh - 1 - i / uw; // rows stored bottom-up
                uint8_t* dst = static_cast<uint8_t*>(lr.pBits) + y * static_cast<size_t>(lr.Pitch) + x * 4;
                dst[0] = b, dst[1] = g, dst[2] = r, dst[3] = a;
            }
            t->UnlockRect(0);
        }
        else if (type >= 0x44585431u && type <= 0x44585435u) // 'DXT1'..'DXT5'
        {
            const D3DFORMAT fmt = static_cast<D3DFORMAT>(MAKEFOURCC('D', 'X', 'T', static_cast<char>('0' + (type - 0x44585430u))));
            const size_t bs = type == 0x44585431u ? 8 : 16;
            const size_t bw = std::max<size_t>(1, (uw + 3) / 4), bh = std::max<size_t>(1, (uh + 3) / 4);
            const size_t src = H + HS + 8;
            if (d.size() < src + bw * bh * bs || FAILED(ctx.device->CreateTexture(uw, uh, 1, 0, fmt, D3DPOOL_MANAGED, &t)) || t == nullptr)
            {
                status_ = "DXT decode failed";
                return false;
            }
            if (FAILED(t->LockRect(0, &lr, nullptr, 0)))
            {
                t->Release();
                return false;
            }
            for (size_t row = 0; row < bh; ++row)
                std::memcpy(static_cast<uint8_t*>(lr.pBits) + row * static_cast<size_t>(lr.Pitch), d.data() + src + row * bw * bs, bw * bs);
            t->UnlockRect(0);
        }
        else
        {
            char b[64]{};
            ::sprintf_s(b, "unsupported DAT image type 0x%08X", type);
            status_ = b;
            return false;
        }
        datTex_ = tex_ = t;
        texW_ = uw, texH_ = uh;
        source_ = std::string("DAT ") + std::to_string(fileId);
        return true;
    }

    void MinimapModule::SelectMap(Context& ctx, uint32_t zone, int32_t floor)
    {
        FreeMap();
        curZone_  = zone;
        curFloor_ = floor;
        hasEntry_ = false;
        source_.clear();
        status_.clear();
        // Find the entry for (zone, floor); first entry of the zone if floor is unknown/unmatched.
        MapEntry first{}, e{};
        bool haveFirst = false;
        for (uint32_t i = 0; i < kMaxEntries && ReadEntry(i, e); ++i)
        {
            if (e.zone != zone)
                continue;
            if (!haveFirst)
                first = e, haveFirst = true;
            if (floor >= 0 && e.floorId == static_cast<uint32_t>(floor))
            {
                entry_ = e, hasEntry_ = true;
                break;
            }
        }
        if (!hasEntry_ && haveFirst)
            entry_ = first, hasEntry_ = true;

        const std::string dir = ctx.resourceDir + "maps\\";
        if (hasEntry_)
        {
            // 1. user override with the same calibration as the game map
            UINT w = 0, h = 0;
            IDirect3DTexture8* t = ctx.textures->File(dir + std::to_string(zone) + "_" + std::to_string(entry_.floorId) + ".png", &w, &h);
            if (t != nullptr && w > 0)
            {
                tex_ = t, texW_ = w, texH_ = h;
                source_ = "override PNG";
                return;
            }
            // 2. the game's own map DAT
            const uint32_t lo  = entry_.flags & 0x0Fu;
            const uint32_t id  = lo == 0 ? 5312u + entry_.datOffset : lo == 1 ? 53295u + entry_.datOffset : lo == 2 ? 54295u + entry_.datOffset : 5522u;
            if (LoadDat(ctx, id))
                return;
        }
        else
            status_ = table_ == 0 ? "map table not found" : "zone has no map";
        // 3. legacy per-zone PNG with ini calibration (handled in Render)
    }

    namespace { MinimapModule* g_minimap = nullptr; }

    void MinimapModule::Initialize(Context& ctx)
    {
        core_     = ctx.core;
        g_minimap = this;
        if (auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr; im != nullptr && im->GetMouse() != nullptr)
            im->GetMouse()->AddCallback("farplaneui_minimap", &MinimapModule::MouseCallback);
    }
    // Ctrl + wheel over the map: counted for Render and kept from the game (TRUE blocks it).
    auto __stdcall MinimapModule::MouseCallback(uint32_t msg, WPARAM w, LPARAM l, bool blocked) -> BOOL
    {
        (void)l, (void)blocked;
        MinimapModule* m = g_minimap;
        if (m == nullptr || msg != WM_MOUSEWHEEL || !m->show_ || !m->mapRect_.valid() || (::GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0)
            return FALSE;
        auto* gui = m->core_ != nullptr ? m->core_->GetGuiManager() : nullptr;
        if (gui == nullptr)
            return FALSE;
        const ImVec2 p = gui->GetIO().MousePos;
        const Rect& r  = m->mapRect_;
        if (p.x < r.x || p.y < r.y || p.x >= r.x + r.w || p.y >= r.y + r.h)
            return FALSE;
        m->wheel_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / static_cast<float>(WHEEL_DELTA);
        return TRUE;
    }
    void MinimapModule::Release(void)
    {
        if (auto* im = core_ != nullptr ? core_->GetInputManager() : nullptr; im != nullptr && im->GetMouse() != nullptr)
            im->GetMouse()->RemoveCallback("farplaneui_minimap");
        g_minimap = nullptr;
        FreeMap();
        if (arrowTex_ != nullptr)
            arrowTex_->Release();
        arrowTex_ = nullptr;
    }

    namespace
    {
        constexpr int kArrowFrames = 64, kArrowCols = 8, kArrowCell = 32;
        // Arrow pointing +x in cell units: tip, upper back corner, notch, lower back corner.
        constexpr float kArrow[4][2] = {{11.0f, 0.0f}, {-8.0f, -7.5f}, {-3.5f, 0.0f}, {-8.0f, 7.5f}};

        auto Edge(float ax, float ay, float bx, float by, float px, float py) -> float { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }
        auto InTri(const float (&a)[2], const float (&b)[2], const float (&c)[2], float px, float py) -> bool
        {
            const float d1 = Edge(a[0], a[1], b[0], b[1], px, py), d2 = Edge(b[0], b[1], c[0], c[1], px, py), d3 = Edge(c[0], c[1], a[0], a[1], px, py);
            return (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
        }
        // The dart is concave: tip-upper-notch + tip-notch-lower. `grow` scales it for the outline.
        auto InArrow(float px, float py, float grow) -> bool
        {
            float p[4][2];
            for (int i = 0; i < 4; ++i)
                p[i][0] = kArrow[i][0] * grow, p[i][1] = kArrow[i][1] * grow;
            return InTri(p[0], p[1], p[2], px, py) || InTri(p[0], p[2], p[3], px, py);
        }
    } // namespace

    // ImDrawList primitives aren't callable from a plugin (no ImGui library to link), so the player
    // arrow is a texture atlas of 64 pre-rotated frames (white fill, dark outline, 4x4 supersampled).
    // Frame k points along screen direction (cos a, sin a), a = k * 2pi / 64.
    void MinimapModule::BuildArrow(Context& ctx)
    {
        constexpr UINT size = kArrowCols * kArrowCell;
        if (ctx.device == nullptr || FAILED(ctx.device->CreateTexture(size, size, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &arrowTex_)) || arrowTex_ == nullptr)
            return;
        D3DLOCKED_RECT lr{};
        if (FAILED(arrowTex_->LockRect(0, &lr, nullptr, 0)))
        {
            arrowTex_->Release();
            arrowTex_ = nullptr;
            return;
        }
        constexpr float kPi = 3.14159265f;
        for (int k = 0; k < kArrowFrames; ++k)
        {
            const float a = static_cast<float>(k) * 2.0f * kPi / kArrowFrames, ca = std::cos(a), sa = std::sin(a);
            const int cx = (k % kArrowCols) * kArrowCell, cy = (k / kArrowCols) * kArrowCell;
            for (int y = 0; y < kArrowCell; ++y)
                for (int x = 0; x < kArrowCell; ++x)
                {
                    int fill = 0, edge = 0;
                    for (int sy = 0; sy < 4; ++sy)
                        for (int sx = 0; sx < 4; ++sx)
                        {
                            // Sample point relative to the cell centre, rotated back into arrow space.
                            const float px = static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / 4.0f - kArrowCell * 0.5f;
                            const float py = static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / 4.0f - kArrowCell * 0.5f;
                            const float lx = px * ca + py * sa, ly = -px * sa + py * ca;
                            if (InArrow(lx, ly, 1.0f))
                                ++fill;
                            else if (InArrow(lx, ly, 1.3f))
                                ++edge;
                        }
                    const float f = fill / 16.0f, e = edge / 16.0f;
                    const float alpha = std::min(1.0f, f + e * 0.85f);
                    const float lum   = alpha > 0 ? f / alpha : 0.0f; // white fill blended over a dark outline
                    auto* dst = static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(cy + y) * static_cast<size_t>(lr.Pitch) + static_cast<size_t>(cx + x) * 4;
                    const auto c = static_cast<uint8_t>(24.0f + lum * 231.0f);
                    dst[0] = c, dst[1] = c, dst[2] = c, dst[3] = static_cast<uint8_t>(alpha * 255.0f);
                }
        }
        arrowTex_->UnlockRect(0);
    }

    void MinimapModule::Load(IConfigurationManager* cfg, const char* alias)
    {
        mod::LoadDock(cfg, alias, "minimap", show_, docked_, locked_, user_);
        zoom_    = std::clamp(cfg->GetFloat(alias, "minimap", "zoom", zoom_), 0.25f, 8.0f);
        enemies_ = cfg->GetBool(alias, "minimap", "enemies", enemies_);
    }
    void MinimapModule::Save(IConfigurationManager* cfg, const char* alias) const
    {
        mod::SaveDock(cfg, alias, "minimap", show_, docked_, locked_, user_);
        ui::SetF(cfg, alias, "minimap", "zoom", zoom_);
        ui::SetB(cfg, alias, "minimap", "enemies", enemies_);
    }
    auto MinimapModule::Command(Context& ctx, const std::vector<std::string>& args) -> bool
    {
        if (mod::DockCommand(ctx, "minimap", args, show_, docked_, locked_))
            return true;
        float f = 0;
        const std::string sub = args.empty() ? std::string() : ui::Lower(args[0]);
        if (sub == "zoom" && args.size() > 1 && ui::ParseFloat(args[1], f))
        {
            zoom_ = std::clamp(f, 0.25f, 8.0f);
            ctx.MarkDirty();
            return true;
        }
        if (sub == "enemies")
        {
            enemies_ = args.size() > 1 ? ui::Lower(args[1]) == "on" : !enemies_;
            ctx.Print(enemies_ ? "Minimap: enemies shown." : "Minimap: enemies hidden.");
            ctx.MarkDirty();
            return true;
        }
        if (sub == "reload")
        {
            curZone_ = 0xFFFFFFFFu;
            ctx.Print("Minimap: map will reload.");
            return true;
        }
        if (sub == "debug")
        {
            char b[320]{};
            ::sprintf_s(b, "Minimap: table=0x%08X floorFn=0x%08X floorThis=0x%08X zone=%u floor=%d entry=%d (floorId=%u idx=%u flags=0x%02X scale=%d datOff=%u off=%d,%d) tex=%ux%u src=%s %s",
                static_cast<unsigned>(table_), static_cast<unsigned>(floorFn_), static_cast<unsigned>(floorThis_), curZone_, curFloor_, hasEntry_ ? 1 : 0,
                entry_.floorId, entry_.floorIndex, entry_.flags, entry_.scale, entry_.datOffset, entry_.offsetX, entry_.offsetY, texW_, texH_,
                source_.empty() ? "none" : source_.c_str(), status_.c_str());
            // Everything for the zone to <Ashita>\logs\farplaneui_minimap.log: every map page of the zone and where
            // the player falls on each (texel position vs the page size), to pick / redirect the right page.
            const std::string dir = std::string(ctx.core->GetInstallPath()) + "\\logs";
            ::CreateDirectoryA(dir.c_str(), nullptr);
            const std::string file = dir + "\\farplaneui_minimap.log";
            FILE* log = nullptr;
            if (::fopen_s(&log, file.c_str(), "wb") == 0 && log != nullptr)
            {
                auto* mm  = ctx.core->GetMemoryManager();
                auto* pty = mm->GetParty();
                auto* ent = mm->GetEntity();
                const uint32_t me = pty->GetMemberTargetIndex(0);
                const float x = ent->GetLocalPositionX(me), y = ent->GetLocalPositionY(me), z = ent->GetLocalPositionZ(me);
                ::fprintf(log, "%s\r\n", b);
                ::fprintf(log, "player x %.2f y %.2f z %.2f, game floor now %d\r\n", static_cast<double>(x), static_cast<double>(y), static_cast<double>(z),
                    CurrentFloor(x, y, z));
                MapEntry e{};
                for (uint32_t i = 0; i < kMaxEntries && ReadEntry(i, e); ++i)
                {
                    if (e.zone != curZone_)
                        continue;
                    const float s  = static_cast<float>(std::abs(e.scale)) / 5.0f;
                    const float tx = x * s - static_cast<float>(e.offsetX), ty = -y * s - static_cast<float>(e.offsetY); // texels on a 512 page
                    const uint32_t lo = e.flags & 0x0Fu;
                    const uint32_t id = lo == 0 ? 5312u + e.datOffset : lo == 1 ? 53295u + e.datOffset : lo == 2 ? 54295u + e.datOffset : 5522u;
                    char path[MAX_PATH]{};
                    ctx.core->GetResourceManager()->GetFilePath(id, path, sizeof(path));
                    ::fprintf(log, "  entry %u: floorId %u index %u flags 0x%02X scale %d datOff %u off %d,%d -> DAT %u (%s); player at %.0f,%.0f of 512%s\r\n", i,
                        e.floorId, e.floorIndex, e.flags, e.scale, e.datOffset, e.offsetX, e.offsetY, id, path[0] != '\0' ? path : "no path",
                        static_cast<double>(tx), static_cast<double>(ty), (tx >= 0 && ty >= 0 && tx < 512 && ty < 512) ? "  <- on this page" : "");
                }
                ::fclose(log);
                ctx.Print("Minimap: details written to " + file);
            }
            return true;
        }
        ctx.Print("/fpui minimap [show|hide|dock|undock|lock|unlock] | zoom <0.25-8> | enemies [on|off] | reload | debug");
        return true;
    }
    void MinimapModule::RenderSettings(Context& ctx, IGuiManager* gui)
    {
        cfg::Grid g(gui, ctx);
        mod::DockSettings(g, ctx, show_, docked_, locked_);
        bool ch = g.Slider("Zoom", "Map magnification. Tip: hold Ctrl and scroll the mouse wheel over the minimap to zoom in and out.", zoom_, 0.25f, 8.0f, "%.2f");
        ch |= g.Toggle("Show enemies", "Nearby monsters as red dots; your target in gold.", enemies_);
        if (ch)
            ctx.MarkDirty();
        g.Info("Map", source_.empty() ? (status_.empty() ? "none" : status_.c_str()) : source_.c_str());
        g.Info("Override", "resources\\farplaneui\\maps\\<zone>_<floor>.png (same layout as the game map)");
    }

    void MinimapModule::Render(Context& ctx, IGuiManager* gui)
    {
        if (!show_)
            return;
        mapRect_ = Rect{}; // set again below while the map is drawn
        auto* mm    = ctx.core->GetMemoryManager();
        auto* party = mm != nullptr ? mm->GetParty() : nullptr;
        auto* ent   = mm != nullptr ? mm->GetEntity() : nullptr;
        if (party == nullptr || ent == nullptr)
            return;
        if (!scanned_)
            Scan(ctx);
        const uint32_t idx  = party->GetMemberTargetIndex(0);
        const uint32_t zone = party->GetMemberZone(0);
        const float x = ent->GetLocalPositionX(idx), y = ent->GetLocalPositionY(idx), z = ent->GetLocalPositionZ(idx);
        const float yaw = ent->GetLocalPositionYaw(idx);

        // Re-evaluate the floor twice a second; reload the map on zone/floor change.
        const ULONGLONG now = ::GetTickCount64();
        if (zone != 0 && (zone != curZone_ || now - lastFloorCheck_ > 500))
        {
            lastFloorCheck_     = now;
            const int32_t floor = CurrentFloor(x, y, z);
            if (zone != curZone_ || (floor >= 0 && floor != curFloor_))
                SelectMap(ctx, zone, floor);
        }

        // No panel border: the ornate minimap frame is the only frame.
        auto p = mod::BeginPanel(ctx, gui, "##fpui_minimap", nullptr, docked_, ctx.layout.minimap, user_, locked_, 0.0f, false, ImGuiWindowFlags_NoScrollWithMouse, false);
        if (p.open)
        {
            // Edge to edge (no panel padding) so the frame's top and bottom line up with the chat box.
            const ImVec2 origin = gui->GetWindowPos();
            const ImVec2 avail  = gui->GetWindowSize();
            const float side    = std::max(16.0f, std::min(avail.x, avail.y));
            // Art kit minimap_frame (160x160, transparent map window 14..145): map goes under it.
            UINT fw = 0, fh = 0;
            IDirect3DTexture8* frame = ember::On() ? nullptr : ctx.Art("minimap_frame", &fw, &fh); // Ember: rounded edge instead
            const float inset = frame != nullptr ? side * 14.0f / 160.0f : 0.0f;
            const ImVec2 mapPos(origin.x + inset, origin.y + inset);
            const float mapSide = frame != nullptr ? side * 131.0f / 160.0f : side;

            IDirect3DTexture8* map = tex_;
            float tw = static_cast<float>(texW_), th = static_cast<float>(texH_);
            // World (x, y) -> map texel: game map calibration, or the legacy per-zone PNG's ini values.
            float s = 0, ox = 0, oy = 0, sc = 1, lox = 0, loy = 0;
            const bool gameMap = map != nullptr && hasEntry_ && texW_ > 0;
            if (gameMap)
            {
                s  = static_cast<float>(std::abs(static_cast<int>(entry_.scale))) / 5.0f;
                ox = static_cast<float>(entry_.offsetX), oy = static_cast<float>(entry_.offsetY);
            }
            else
            {
                UINT w = 0, h = 0;
                map = ctx.textures->File(ctx.resourceDir + "maps\\" + std::to_string(zone) + ".png", &w, &h);
                if (map != nullptr && w > 0)
                {
                    tw = static_cast<float>(w), th = static_cast<float>(h);
                    const std::string zk = "z" + std::to_string(zone) + "_";
                    auto* cfg = ctx.core->GetConfigurationManager();
                    sc  = cfg->GetFloat(ctx.alias.c_str(), "minimap", (zk + "scale").c_str(), 1.0f);
                    lox = cfg->GetFloat(ctx.alias.c_str(), "minimap", (zk + "ox").c_str(), tw * 0.5f);
                    loy = cfg->GetFloat(ctx.alias.c_str(), "minimap", (zk + "oy").c_str(), th * 0.5f);
                }
            }
            const auto toTexel = [&](float wx, float wy, float& tx, float& ty) {
                if (gameMap)
                {
                    tx = (wx * s - ox) * tw / 512.0f;
                    ty = (-wy * s - oy) * th / 512.0f;
                }
                else
                {
                    tx = lox + wx * sc;
                    ty = loy - wy * sc;
                }
            };
            float px = 0, py = 0;
            toTexel(x, y, px, py);
            // Map grid square for the clock: 32-texel squares on the 512 map, columns A.., rows 1.. (e.g. H-10).
            ctx.mapGrid.clear();
            if (gameMap && tw > 0 && th > 0)
            {
                const int col = static_cast<int>(std::floor(px * 512.0f / tw / 32.0f)), row = static_cast<int>(std::floor(py * 512.0f / th / 32.0f)) + 1;
                if (col >= 0 && col < 26 && row >= 1)
                    ctx.mapGrid = std::string(1, static_cast<char>('A' + col)) + "-" + std::to_string(row);
            }
            if (ctx.white != nullptr)
                ui::ImageAt(gui, ctx.white, mapPos, ImVec2(mapSide, mapSide), ImVec2(0, 0), ImVec2(1, 1), art::navy, 0.6f);
            if (map != nullptr && tw > 0 && th > 0)
            {
                // Show mapSide/zoom texels around the player (1 texel = 1 px at zoom 1 for a 512 map).
                const float halfU = mapSide * 0.5f / zoom_ / tw, halfV = mapSide * 0.5f / zoom_ / th;
                const float u = px / tw, v = py / th;
                ui::ImageAt(gui, map, mapPos, ImVec2(mapSide, mapSide), ImVec2(u - halfU, v - halfV), ImVec2(u + halfU, v + halfV), Rgba{1, 1, 1, 1});
            }
            // Map texels are drawn at `zoom_` screen pixels each, centred on the player.
            const ImVec2 mapCenter(mapPos.x + mapSide * 0.5f, mapPos.y + mapSide * 0.5f);
            // Nearby monsters (spawn flag 0x10) that the game is rendering and are alive; the current
            // target is drawn larger in gold.
            if (enemies_ && ctx.white != nullptr && map != nullptr)
            {
                const uint32_t tgt = mm->GetTarget() != nullptr ? mm->GetTarget()->GetTargetIndex(0) : 0;
                for (const uint32_t i : mod::NearbyMobs(ctx))
                {
                    if (i == idx || ent->GetRawEntity(i) == nullptr || (ent->GetSpawnFlags(i) & 0x10u) == 0 ||
                        (ent->GetRenderFlags0(i) & 0x4200u) != 0x200u || ent->GetHPPercent(i) == 0)
                        continue;
                    float ex = 0, ey = 0;
                    toTexel(ent->GetLocalPositionX(i), ent->GetLocalPositionY(i), ex, ey);
                    const float sx = mapCenter.x + (ex - px) * zoom_, sy = mapCenter.y + (ey - py) * zoom_;
                    const float d  = i == tgt ? 6.0f : 4.0f;
                    if (sx - d < mapPos.x || sy - d < mapPos.y || sx + d > mapPos.x + mapSide || sy + d > mapPos.y + mapSide)
                        continue;
                    ui::ImageAt(gui, ctx.white, ImVec2(sx - d * 0.5f, sy - d * 0.5f), ImVec2(d, d), ImVec2(0, 0), ImVec2(1, 1),
                        i == tgt ? art::gold_hi : Rgba{0.86f, 0.22f, 0.18f, 1.0f});
                }
            }
            if (frame != nullptr)
                ui::ImageAt(gui, frame, origin, ImVec2(side, side), ImVec2(0, 0), ImVec2(1, 1), Rgba{1, 1, 1, 1});
            else if (ember::On())
            {
                // Rounded corners + edge, and the grid square as a chip in the bottom-right corner.
                const auto& t = ember::T();
                ember::RoundFrame(ctx, gui, origin, ImVec2(side, side));
                if (!ctx.mapGrid.empty())
                {
                    gui->PushFont(nullptr, std::max(9.0f, t.S(11)));
                    const ImVec2 cs(std::max(t.S(44), gui->CalcTextSize(ctx.mapGrid.c_str()).x + t.S(14)), std::max(t.S(15), gui->GetTextLineHeight() + 2));
                    const ImVec2 cp(origin.x + side - cs.x - t.S(6), origin.y + side - cs.y - t.S(6));
                    ember::Track(ctx, gui, cp, cs);
                    ember::TextCenter(gui, cp.x + cs.x * 0.5f, cp.y + (cs.y - gui->GetTextLineHeight()) * 0.5f, t.amber, ctx.mapGrid.c_str());
                    gui->PopFont();
                }
            }
            // Player arrow: screen direction (cos h, sin h) - 0 = east, +h turns south (boussole's
            // cursor rotation of heading + pi/2) - picked from the pre-rotated atlas.
            if (arrowTex_ == nullptr)
                BuildArrow(ctx);
            if (arrowTex_ != nullptr)
            {
                constexpr float kTwoPi = 6.28318531f;
                float h = std::fmod(yaw, kTwoPi);
                if (h < 0)
                    h += kTwoPi;
                const int k = static_cast<int>(std::lround(h / kTwoPi * kArrowFrames)) % kArrowFrames;
                const float cu = 1.0f / kArrowCols;
                const ImVec2 uv0(static_cast<float>(k % kArrowCols) * cu, static_cast<float>(k / kArrowCols) * cu);
                const float a = 24.0f; // on-screen size of one 32px cell (arrow ~18px long)
                ui::ImageAt(gui, arrowTex_, ImVec2(mapCenter.x - a * 0.5f, mapCenter.y - a * 0.5f), ImVec2(a, a), uv0, ImVec2(uv0.x + cu, uv0.y + cu),
                    Rgba{1, 1, 1, 1});
            }
            gui->Dummy(ImVec2(side, side));
            if (map == nullptr && gui->IsItemHovered())
                gui->SetTooltip("No map for zone %u: %s. /fpui minimap debug for details.", zone, status_.empty() ? "unknown" : status_.c_str());
            // Ctrl + mouse wheel zooms (15% a notch; notches come from MouseCallback). Resting the mouse on
            // the map for a moment shows the hint. Both use the map's own rectangle, not ImGui's hover state,
            // so a window overlapping the map doesn't stop them.
            mapRect_ = Rect{origin.x, origin.y, side, side};
            if (wheel_ != 0.0f)
            {
                zoom_  = std::clamp(zoom_ * std::pow(1.15f, wheel_), 0.25f, 8.0f);
                wheel_ = 0.0f;
                ctx.MarkDirty();
            }
            const ImVec2 mp = gui->GetIO().MousePos;
            const bool over = mp.x >= origin.x && mp.y >= origin.y && mp.x < origin.x + side && mp.y < origin.y + side;
            const double nowS = static_cast<double>(::GetTickCount64()) / 1000.0;
            if (!over || std::abs(mp.x - hoverPos_.x) > 3.0f || std::abs(mp.y - hoverPos_.y) > 3.0f)
                hoverAt_ = nowS, hoverPos_ = over ? mp : ImVec2(-1, -1);
            if (over && map != nullptr && nowS - hoverAt_ > 0.6)
                gui->SetTooltip("Ctrl + mouse wheel to zoom (%.2fx)", static_cast<double>(zoom_));
        }
        mod::EndPanel(p, &user_, docked_, ctx);
    }
} // namespace farplaneui
