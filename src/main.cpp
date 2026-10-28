// Liberty Edge Resolution Unlock for GTA IV (Complete Edition)
//
// Supersampling for GPUs without a driver-level option (e.g. Intel Arc, which has no DSR/VSR equivalent):
//  1. adds resolutions above the monitor (1.5x, 2x, 3x by default) to the adapter mode list, so the game's
//     own Graphics > Video Mode menu offers them and saves the choice;
//  2. keeps the window at monitor size (the game would otherwise make it as big as the resolution) and,
//     if the game asks for exclusive fullscreen at one of these modes, makes it windowed instead;
//  3. scales the mouse cursor position from the window to the game resolution;
//  4. right before Present, replaces the backbuffer with a proper linear-light box-filtered downsample
//     (plus optional contrast adaptive sharpening), because DXVK's present blit would otherwise pick a single
//     texel per output pixel and throw the extra resolution away.
//
// Standalone ASI: no dependency on a specific FusionFix version. Loaded by any ASI loader (FusionFix ships one).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include "MinHook.h"
#include "downsample_ps.h" // g_main: compiled src/downsample.hlsl (ps_3_0)

#define LE_VERSION "1.0"

namespace
{
    // ---------------------------------------------------------------- settings / log
    struct Settings
    {
        bool enabled = true;
        std::vector<float> scales = { 1.5f, 2.0f, 3.0f };
        bool filter = true;
        float sharpen = 0.3f;
        bool fitWindow = true;
        bool mouseFix = true;
        bool forceWindowed = true;
    } cfg;

    std::wstring gDir;

    void Log(const char* fmt, ...)
    {
        static int lines = 0;
        if (++lines > 200) return;
        char b[512];
        va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
        FILE* f = nullptr;
        if (_wfopen_s(&f, (gDir + L"LibertyEdge.ResolutionUnlock.log").c_str(), lines == 1 ? L"w" : L"a") == 0 && f)
        {
            fprintf(f, "%s\n", b);
            fclose(f);
        }
    }

    void LoadSettings(HMODULE self)
    {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(self, path, MAX_PATH);
        std::wstring ini = path;
        ini = ini.substr(0, ini.find_last_of(L'.')) + L".ini";
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        gDir = path;
        gDir = gDir.substr(0, gDir.find_last_of(L"\\/") + 1);

        auto getInt = [&](const wchar_t* k, int d) { return (int)GetPrivateProfileIntW(L"MAIN", k, d, ini.c_str()); };
        auto getStr = [&](const wchar_t* k, const wchar_t* d) {
            wchar_t b[256];
            GetPrivateProfileStringW(L"MAIN", k, d, b, 256, ini.c_str());
            std::wstring s = b;
            auto c = s.find(L"//"); if (c != std::wstring::npos) s = s.substr(0, c);
            c = s.find(L';'); if (c != std::wstring::npos) s = s.substr(0, c);
            return s;
        };
        cfg.enabled = getInt(L"Enabled", 1) != 0;
        cfg.filter = getInt(L"Filter", 1) != 0;
        cfg.fitWindow = getInt(L"FitWindow", 1) != 0;
        cfg.mouseFix = getInt(L"MouseFix", 1) != 0;
        cfg.forceWindowed = getInt(L"ForceWindowed", 1) != 0;
        cfg.sharpen = std::clamp((float)_wtof(getStr(L"Sharpen", L"0.3").c_str()), 0.0f, 1.0f);
        auto sc = getStr(L"Scales", L"1.5, 2, 3");
        std::vector<float> scales;
        for (wchar_t* ctx = nullptr, *t = wcstok_s(sc.data(), L" ,\t", &ctx); t; t = wcstok_s(nullptr, L" ,\t", &ctx))
        {
            float v = (float)_wtof(t);
            if (v > 1.01f && v <= 4.0f) scales.push_back(v);
        }
        if (!scales.empty()) cfg.scales = scales;
        Log("Liberty Edge Resolution Unlock %s | Enabled=%d Filter=%d Sharpen=%.2f FitWindow=%d MouseFix=%d ForceWindowed=%d scales=%u",
            LE_VERSION, cfg.enabled, cfg.filter, cfg.sharpen, cfg.fitWindow, cfg.mouseFix, cfg.forceWindowed, (unsigned)cfg.scales.size());
    }

    // ---------------------------------------------------------------- shared state
    HWND gWnd = nullptr;          // device window
    UINT gNativeW = 0, gNativeH = 0; // largest real mode of the monitor
    UINT gBackW = 0, gBackH = 0;  // current backbuffer size
    bool InVirtualMode() { return gNativeW && (gBackW > gNativeW || gBackH > gNativeH); }

    void* PatchVtbl(void* obj, int idx, void* fn)
    {
        void** vtbl = *(void***)obj;
        if (vtbl[idx] == fn) return nullptr;
        DWORD old;
        VirtualProtect(&vtbl[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
        void* orig = vtbl[idx];
        vtbl[idx] = fn;
        VirtualProtect(&vtbl[idx], sizeof(void*), old, &old);
        return orig;
    }

    bool MonitorRect(HWND w, RECT& r)
    {
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfoW(MonitorFromWindow(w ? w : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST), &mi)) return false;
        r = mi.rcMonitor;
        return true;
    }

    // ---------------------------------------------------------------- user32 hooks (window size, mouse)
    using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
    using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
    using ScreenToClient_t = BOOL(WINAPI*)(HWND, LPPOINT);
    MoveWindow_t oMoveWindow = nullptr;
    SetWindowPos_t oSetWindowPos = nullptr;
    ScreenToClient_t oScreenToClient = nullptr;

    bool ShouldFit(HWND w, int cx, int cy, RECT& mon)
    {
        if (!cfg.fitWindow || !w || w != gWnd || !InVirtualMode() || !MonitorRect(w, mon)) return false;
        return cx > mon.right - mon.left || cy > mon.bottom - mon.top;
    }

    BOOL WINAPI hkMoveWindow(HWND w, int x, int y, int cx, int cy, BOOL repaint)
    {
        RECT m;
        if (ShouldFit(w, cx, cy, m))
            return oMoveWindow(w, m.left, m.top, m.right - m.left, m.bottom - m.top, repaint);
        return oMoveWindow(w, x, y, cx, cy, repaint);
    }

    BOOL WINAPI hkSetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags)
    {
        RECT m;
        if (!(flags & SWP_NOSIZE) && ShouldFit(w, cx, cy, m))
            return oSetWindowPos(w, after, m.left, m.top, m.right - m.left, m.bottom - m.top, flags & ~SWP_NOMOVE);
        return oSetWindowPos(w, after, x, y, cx, cy, flags);
    }

    BOOL WINAPI hkScreenToClient(HWND w, LPPOINT p)
    {
        BOOL r = oScreenToClient(w, p);
        if (r && p && cfg.mouseFix && w && w == gWnd && InVirtualMode())
        {
            RECT cr;
            if (GetClientRect(w, &cr) && cr.right > 0 && cr.bottom > 0 && (UINT)cr.right < gBackW)
            {
                p->x = MulDiv(p->x, gBackW, cr.right);
                p->y = MulDiv(p->y, gBackH, cr.bottom);
            }
        }
        return r;
    }

    void FitWindowNow()
    {
        if (!gWnd) return;
        RECT wr, m;
        if (GetWindowRect(gWnd, &wr) && ShouldFit(gWnd, wr.right - wr.left, wr.bottom - wr.top, m))
        {
            oSetWindowPos(gWnd, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_NOZORDER | SWP_NOACTIVATE);
            Log("window fitted to the monitor (%ldx%ld)", m.right - m.left, m.bottom - m.top);
        }
    }

    // ---------------------------------------------------------------- virtual video modes
    using GetAdapterModeCount_t = UINT(WINAPI*)(IDirect3D9*, UINT, D3DFORMAT);
    using EnumAdapterModes_t = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DFORMAT, UINT, D3DDISPLAYMODE*);
    using CreateDevice_t = HRESULT(WINAPI*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    GetAdapterModeCount_t oGetAdapterModeCount = nullptr;
    EnumAdapterModes_t oEnumAdapterModes = nullptr;
    CreateDevice_t oCreateDevice = nullptr;

    struct ModeList { UINT adapter = ~0u; D3DFORMAT fmt = D3DFMT_UNKNOWN; UINT native = 0; std::vector<D3DDISPLAYMODE> extra; };
    ModeList gModes;

    void BuildModes(IDirect3D9* d3d, UINT adapter, D3DFORMAT fmt, UINT n)
    {
        gModes.adapter = adapter; gModes.fmt = fmt; gModes.native = n; gModes.extra.clear();
        D3DDISPLAYMODE best = {};
        for (UINT i = 0; i < n; i++)
        {
            D3DDISPLAYMODE dm = {};
            if (oEnumAdapterModes(d3d, adapter, fmt, i, &dm) == D3D_OK)
                if (dm.Width * dm.Height > best.Width * best.Height || (dm.Width == best.Width && dm.Height == best.Height && dm.RefreshRate > best.RefreshRate))
                    best = dm;
        }
        if (!best.Width) return;
        if (adapter == 0 || !gNativeW) { gNativeW = best.Width; gNativeH = best.Height; }
        for (float s : cfg.scales)
        {
            D3DDISPLAYMODE dm = best;
            dm.Width = UINT(best.Width * s + 0.5f) & ~1u;
            dm.Height = UINT(best.Height * s + 0.5f) & ~1u;
            if (dm.Width <= best.Width || dm.Width > 7680 || dm.Height > 4320) continue;
            gModes.extra.push_back(dm);
        }
        static int logged = 0;
        if (logged++ < 4)
            Log("adapter %u format %d: %u modes, monitor %ux%u@%u, +%u virtual", adapter, (int)fmt, n, best.Width, best.Height, best.RefreshRate, (unsigned)gModes.extra.size());
    }

    UINT WINAPI hkGetAdapterModeCount(IDirect3D9* d3d, UINT adapter, D3DFORMAT fmt)
    {
        UINT n = oGetAdapterModeCount(d3d, adapter, fmt);
        BuildModes(d3d, adapter, fmt, n);
        return n + (UINT)gModes.extra.size();
    }

    HRESULT WINAPI hkEnumAdapterModes(IDirect3D9* d3d, UINT adapter, D3DFORMAT fmt, UINT mode, D3DDISPLAYMODE* out)
    {
        if (adapter != gModes.adapter || fmt != gModes.fmt)
            BuildModes(d3d, adapter, fmt, oGetAdapterModeCount(d3d, adapter, fmt));
        if (out && mode >= gModes.native && mode < gModes.native + gModes.extra.size())
        {
            *out = gModes.extra[mode - gModes.native];
            out->Format = fmt;
            return D3D_OK;
        }
        return oEnumAdapterModes(d3d, adapter, fmt, mode, out);
    }

    void PrepareParams(D3DPRESENT_PARAMETERS* pp, const char* what)
    {
        if (!pp) return;
        gBackW = pp->BackBufferWidth; gBackH = pp->BackBufferHeight;
        if (pp->hDeviceWindow) gWnd = pp->hDeviceWindow;
        if (!gNativeW)
        {
            RECT m;
            if (MonitorRect(gWnd, m)) { gNativeW = m.right - m.left; gNativeH = m.bottom - m.top; }
        }
        if (InVirtualMode() && !pp->Windowed && cfg.forceWindowed)
        {
            pp->Windowed = TRUE;
            pp->FullScreen_RefreshRateInHz = 0;
            Log("%s: %ux%u is above the monitor, switched exclusive fullscreen to windowed", what, gBackW, gBackH);
        }
        Log("%s: backbuffer %ux%u, monitor %ux%u%s", what, gBackW, gBackH, gNativeW, gNativeH, InVirtualMode() ? " (supersampling)" : "");
    }

    // ---------------------------------------------------------------- downsample filter
    using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using Reset_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    Present_t oPresent = nullptr;
    Reset_t oReset = nullptr;

    struct Filter
    {
        IDirect3DPixelShader9* ps = nullptr;
        IDirect3DTexture9* tex = nullptr;
        IDirect3DSurface9* surf = nullptr;
        IDirect3DStateBlock9* sb = nullptr;
        UINT w = 0, h = 0; D3DFORMAT fmt = D3DFMT_UNKNOWN;
        bool broken = false;
        int lastReason = -1;

        void Reason(int code, const char* msg) { if (code != lastReason) { lastReason = code; Log("filter: %s", msg); } }

        void ReleaseDefault()
        {
            if (surf) { surf->Release(); surf = nullptr; }
            if (tex) { tex->Release(); tex = nullptr; }
            if (sb) { sb->Release(); sb = nullptr; }
            w = h = 0;
        }

        void Run(IDirect3DDevice9* dev, HWND presentWnd)
        {
            if (!cfg.filter || broken) return;
            HWND wnd = presentWnd ? presentWnd : gWnd;
            RECT cr;
            if (!wnd || !GetClientRect(wnd, &cr) || cr.right <= 0 || cr.bottom <= 0) return;

            IDirect3DSurface9* bb = nullptr;
            if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) { Reason(1, "GetBackBuffer failed"); return; }
            D3DSURFACE_DESC d{};
            bb->GetDesc(&d);
            if (d.Width <= (UINT)cr.right && d.Height <= (UINT)cr.bottom) { bb->Release(); Reason(2, "backbuffer not larger than the window, nothing to do"); return; }

            if (!ps && FAILED(dev->CreatePixelShader((const DWORD*)g_main, &ps))) { broken = true; bb->Release(); Log("filter: CreatePixelShader FAILED, filter disabled"); return; }
            if (!sb && FAILED(dev->CreateStateBlock(D3DSBT_ALL, &sb))) { broken = true; bb->Release(); Log("filter: CreateStateBlock FAILED, filter disabled"); return; }
            if (!tex || w != d.Width || h != d.Height || fmt != d.Format)
            {
                if (surf) { surf->Release(); surf = nullptr; }
                if (tex) { tex->Release(); tex = nullptr; }
                if (FAILED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &tex, nullptr)) || FAILED(tex->GetSurfaceLevel(0, &surf)))
                {
                    if (tex) { tex->Release(); tex = nullptr; }
                    broken = true; bb->Release(); Log("filter: CreateTexture FAILED, filter disabled"); return;
                }
                w = d.Width; h = d.Height; fmt = d.Format;
            }
            if (FAILED(dev->StretchRect(bb, nullptr, surf, nullptr, D3DTEXF_POINT))) { bb->Release(); Reason(3, "StretchRect failed"); return; }

            sb->Capture();
            IDirect3DSurface9* oldRT = nullptr; IDirect3DSurface9* oldDS = nullptr;
            dev->GetRenderTarget(0, &oldRT);
            dev->GetDepthStencilSurface(&oldDS);

            dev->BeginScene();
            dev->SetRenderTarget(0, bb);
            dev->SetDepthStencilSurface(nullptr);
            dev->SetVertexShader(nullptr);
            dev->SetPixelShader(ps);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            dev->SetTexture(0, tex);
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
            const DWORD rs[][2] = {
                { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE },
                { D3DRS_ALPHABLENDENABLE, FALSE }, { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE },
                { D3DRS_COLORWRITEENABLE, 0xF }, { D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_SCISSORTESTENABLE, FALSE },
                { D3DRS_FOGENABLE, FALSE }, { D3DRS_CLIPPLANEENABLE, 0 }, { D3DRS_FILLMODE, D3DFILL_SOLID },
            };
            for (auto& s : rs) dev->SetRenderState((D3DRENDERSTATETYPE)s[0], s[1]);

            float rx = (float)d.Width / cr.right, ry = (float)d.Height / cr.bottom;
            float taps = std::clamp(std::ceil(std::max(rx, ry) - 0.01f), 1.0f, 3.0f);
            const float c[12] = {
                (float)d.Width, (float)d.Height, 1.0f / d.Width, 1.0f / d.Height,
                rx, ry, (float)cr.right, (float)cr.bottom,
                cfg.sharpen, taps, 0.0f, 0.0f };
            dev->SetPixelShaderConstantF(0, c, 3);

            struct V { float x, y, z, rhw, u, v; };
            const float W = (float)d.Width - 0.5f, H = (float)d.Height - 0.5f;
            V q[4] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { W, -0.5f, 0, 1, 1, 0 }, { -0.5f, H, 0, 1, 0, 1 }, { W, H, 0, 1, 1, 1 } };
            HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
            dev->EndScene();

            dev->SetTexture(0, nullptr);
            dev->SetRenderTarget(0, oldRT ? oldRT : bb);
            dev->SetDepthStencilSurface(oldDS);
            if (oldRT) oldRT->Release();
            if (oldDS) oldDS->Release();
            sb->Apply();
            bb->Release();

            if (SUCCEEDED(hr))
            {
                char m[160];
                snprintf(m, sizeof(m), "active %ux%u -> %ldx%ld (%u taps per axis, sharpen %.2f)", d.Width, d.Height, cr.right, cr.bottom, (unsigned)taps, cfg.sharpen);
                Reason(0, m);
            }
            else
                Reason(4, "DrawPrimitiveUP failed");
        }
    } gFilter;

    HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
    {
        gFilter.Run(dev, wnd);
        return oPresent(dev, src, dst, wnd, dirty);
    }

    HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
    {
        gFilter.ReleaseDefault();
        PrepareParams(pp, "Reset");
        HRESULT hr = oReset(dev, pp);
        if (SUCCEEDED(hr)) FitWindowNow();
        else Log("Reset failed: 0x%08lX", (unsigned long)hr);
        return hr;
    }

    HRESULT WINAPI hkCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
    {
        if (pp && !pp->hDeviceWindow) gWnd = focus;
        PrepareParams(pp, "CreateDevice");
        if (!gWnd) gWnd = focus;
        HRESULT hr = oCreateDevice(d3d, adapter, type, focus, flags, pp, out);
        if (SUCCEEDED(hr) && out && *out)
        {
            if (auto o = PatchVtbl(*out, 16, (void*)&hkReset)) oReset = (Reset_t)o;
            if (auto o = PatchVtbl(*out, 17, (void*)&hkPresent)) oPresent = (Present_t)o;
            FitWindowNow();
        }
        else
            Log("CreateDevice failed: 0x%08lX", (unsigned long)hr);
        return hr;
    }

    // ---------------------------------------------------------------- entry
    using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
    Direct3DCreate9_t oDirect3DCreate9 = nullptr;

    IDirect3D9* WINAPI hkDirect3DCreate9(UINT sdk)
    {
        IDirect3D9* d3d = oDirect3DCreate9(sdk);
        if (d3d)
        {
            if (auto o = PatchVtbl(d3d, 6, (void*)&hkGetAdapterModeCount)) oGetAdapterModeCount = (GetAdapterModeCount_t)o;
            if (auto o = PatchVtbl(d3d, 7, (void*)&hkEnumAdapterModes)) oEnumAdapterModes = (EnumAdapterModes_t)o;
            if (auto o = PatchVtbl(d3d, 16, (void*)&hkCreateDevice)) oCreateDevice = (CreateDevice_t)o;
            static bool once = false;
            if (!once) { once = true; Log("Direct3DCreate9: hooks installed"); }
        }
        return d3d;
    }

    void Install(HMODULE self)
    {
        LoadSettings(self);
        if (!cfg.enabled) { Log("disabled in the ini"); return; }
        if (MH_Initialize() != MH_OK) { Log("MinHook init failed"); return; }
        if (!GetModuleHandleW(L"d3d9.dll")) LoadLibraryW(L"d3d9.dll");
        MH_STATUS s = MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9", (void*)&hkDirect3DCreate9, (void**)&oDirect3DCreate9);
        if (s != MH_OK) Log("hook Direct3DCreate9 failed (%d)", (int)s);
        MH_CreateHookApi(L"user32.dll", "MoveWindow", (void*)&hkMoveWindow, (void**)&oMoveWindow);
        MH_CreateHookApi(L"user32.dll", "SetWindowPos", (void*)&hkSetWindowPos, (void**)&oSetWindowPos);
        MH_CreateHookApi(L"user32.dll", "ScreenToClient", (void*)&hkScreenToClient, (void**)&oScreenToClient);
        MH_EnableHook(MH_ALL_HOOKS);
    }
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(mod);
        Install(mod);
    }
    return TRUE;
}
