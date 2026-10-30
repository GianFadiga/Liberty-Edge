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
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "MinHook.h"
#include "downsample_ps.h" // g_main: compiled src/downsample.hlsl (ps_3_0)

#define LE_VERSION "1.7"

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
        bool keyboardFix = true;
        bool log = false; // diagnostic files next to GTAIV.exe (log, previous log, crash log)
        int overlayKey = VK_F11;
    } cfg;

    std::wstring gDir;

    void Log(const char* fmt, ...)
    {
        static int lines = 0;
        if (!cfg.log || ++lines > 200) return;
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
        cfg.keyboardFix = getInt(L"KeyboardFix", 1) != 0;
        cfg.log = getInt(L"Log", 0) != 0;
        // keep the previous session's log (useful after a crash: the next launch would overwrite it)
        if (cfg.log)
            MoveFileExW((gDir + L"LibertyEdge.ResolutionUnlock.log").c_str(), (gDir + L"LibertyEdge.ResolutionUnlock.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
        cfg.overlayKey = (int)wcstol(getStr(L"ModListKey", L"0x7A").c_str(), nullptr, 0);
        cfg.sharpen = std::clamp((float)_wtof(getStr(L"Sharpen", L"0.3").c_str()), 0.0f, 1.0f);
        auto sc = getStr(L"Scales", L"1.5, 2, 3");
        std::vector<float> scales;
        for (wchar_t* ctx = nullptr, *t = wcstok_s(sc.data(), L" ,\t", &ctx); t; t = wcstok_s(nullptr, L" ,\t", &ctx))
        {
            float v = (float)_wtof(t);
            if (v > 1.01f && v <= 4.0f) scales.push_back(v);
        }
        if (!scales.empty()) cfg.scales = scales;
        Log("Liberty Edge Resolution Unlock %s | Enabled=%d Filter=%d Sharpen=%.2f FitWindow=%d MouseFix=%d ForceWindowed=%d KeyboardFix=%d scales=%u",
            LE_VERSION, cfg.enabled, cfg.filter, cfg.sharpen, cfg.fitWindow, cfg.mouseFix, cfg.forceWindowed, cfg.keyboardFix, (unsigned)cfg.scales.size());
    }

    // ---------------------------------------------------------------- shared state
    HWND gWnd = nullptr;          // device window
    UINT gNativeW = 0, gNativeH = 0; // largest real mode of the monitor
    UINT gBackW = 0, gBackH = 0;  // current backbuffer size
    bool InVirtualMode() { return gNativeW && (gBackW > gNativeW || gBackH > gNativeH); }
    bool gForcedWindowed = false; // the game asked for exclusive fullscreen and got a borderless window instead

    // ---------------------------------------------------------------- crash log
    // Writes where a crash happened (module + offset, plus return addresses found on the stack) to
    // LibertyEdge.ResolutionUnlock.crash.log, so a crash can be attributed to the game, a mod or a driver.
    std::wstring ModuleOf(DWORD_PTR addr, DWORD_PTR& base)
    {
        HMODULE m = nullptr; base = 0;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m) || !m) return L"";
        wchar_t p[MAX_PATH]; GetModuleFileNameW(m, p, MAX_PATH);
        std::wstring n = p; n = n.substr(n.find_last_of(L"\\/") + 1);
        base = (DWORD_PTR)m;
        return n;
    }

    bool SafeRead(const DWORD_PTR* p, DWORD_PTR& v)
    {
        __try { v = *p; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    LONG CALLBACK CrashHandler(PEXCEPTION_POINTERS ep)
    {
        DWORD code = ep->ExceptionRecord->ExceptionCode;
        if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409)
            return EXCEPTION_CONTINUE_SEARCH;
        static LONG n = 0;
        if (InterlockedIncrement(&n) > 8) return EXCEPTION_CONTINUE_SEARCH;
        FILE* f = nullptr;
        if (_wfopen_s(&f, (gDir + L"LibertyEdge.ResolutionUnlock.crash.log").c_str(), L"a") != 0 || !f) return EXCEPTION_CONTINUE_SEARCH;
        SYSTEMTIME t; GetLocalTime(&t);
        DWORD_PTR ip = (DWORD_PTR)ep->ExceptionRecord->ExceptionAddress, base = 0;
        std::wstring mod = ModuleOf(ip, base);
        fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u  exception 0x%08lX at %ls+0x%lX (thread %lu, backbuffer %ux%u, forced windowed %d)\n",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, code, mod.empty() ? L"?" : mod.c_str(), (unsigned long)(ip - base),
            GetCurrentThreadId(), gBackW, gBackH, (int)gForcedWindowed);
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
            fprintf(f, "  %s address 0x%08lX\n", ep->ExceptionRecord->ExceptionInformation[0] == 8 ? "execute" : ep->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read from",
                (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
#ifdef _M_IX86
        DWORD_PTR* sp = (DWORD_PTR*)ep->ContextRecord->Esp;
        int found = 0;
        for (int i = 0; i < 512 && found < 12; i++)
        {
            DWORD_PTR v = 0;
            if (!SafeRead(sp + i, v)) break;
            MEMORY_BASIC_INFORMATION mbi;
            if (v < 0x10000 || !VirtualQuery((void*)v, &mbi, sizeof(mbi)) || !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;
            DWORD_PTR b = 0; std::wstring m = ModuleOf(v, b);
            if (m.empty()) continue;
            fprintf(f, "  stack: %ls+0x%lX\n", m.c_str(), (unsigned long)(v - b));
            found++;
        }
#endif
        fprintf(f, "  (first-chance: the game may still have handled it)\n");
        fclose(f);
        return EXCEPTION_CONTINUE_SEARCH;
    }

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

    // The game treats a WM_SIZE in windowed/borderless mode as a resolution change: after the window is
    // fitted to the monitor it would reset the device to the monitor size and save that as the Video Mode.
    // While a mode above the monitor is active, WM_SIZE is reported with the game resolution instead.
    WNDPROC gOrigWndProc = nullptr;
    HWND gSubclassed = nullptr;
    bool gWndUnicode = false;

    LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_SIZE && wp != SIZE_MINIMIZED && InVirtualMode())
        {
            UINT cx = LOWORD(lp), cy = HIWORD(lp);
            if (cx && cy && (cx != gBackW || cy != gBackH))
            {
                static int n = 0;
                if (n++ < 10) Log("WM_SIZE %ux%u reported to the game as %ux%u", cx, cy, gBackW, gBackH);
                lp = MAKELPARAM(gBackW, gBackH);
            }
        }
        return gWndUnicode ? CallWindowProcW(gOrigWndProc, w, msg, wp, lp) : CallWindowProcA(gOrigWndProc, w, msg, wp, lp);
    }

    void Subclass()
    {
        if (!gWnd || gWnd == gSubclassed) return;
        gWndUnicode = IsWindowUnicode(gWnd) != 0;
        LONG_PTR prev = gWndUnicode ? SetWindowLongPtrW(gWnd, GWLP_WNDPROC, (LONG_PTR)&WndProc) : SetWindowLongPtrA(gWnd, GWLP_WNDPROC, (LONG_PTR)&WndProc);
        if (prev)
        {
            gOrigWndProc = (WNDPROC)prev;
            gSubclassed = gWnd;
            Log("window procedure hooked (%s)", gWndUnicode ? "unicode" : "ansi");
        }
    }

    void FitWindowNow()
    {
        if (!gWnd) return;
        RECT wr, m;
        if (gForcedWindowed && GetWindowRect(gWnd, &wr) && MonitorRect(gWnd, m) && !EqualRect(&wr, &m))
        {
            oSetWindowPos(gWnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_NOACTIVATE);
            Log("borderless window placed over the monitor (%ldx%ld)", m.right - m.left, m.bottom - m.top);
        }
        else if (GetWindowRect(gWnd, &wr) && ShouldFit(gWnd, wr.right - wr.left, wr.bottom - wr.top, m))
        {
            oSetWindowPos(gWnd, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_NOZORDER | SWP_NOACTIVATE);
            Log("window fitted to the monitor (%ldx%ld)", m.right - m.left, m.bottom - m.top);
        }
    }

    // ---------------------------------------------------------------- keyboard (system-wide side effects)
    // GTA IV installs a low-level keyboard hook that sees every key typed in any program and translates it
    // with ToAsciiEx/ToUnicodeEx, which consumes Windows' dead-key state: accents (´ ~ ^ on ABNT2 and other
    // layouts with dead keys) stop working everywhere while the game is open, and the game keeps reacting to
    // keys after alt-tab. The game's hook procedure is now only called while the game is in the foreground.
    // It also loads/activates keyboard layouts; activation is kept off so the user's layout is not changed.
    using SetWindowsHookExA_t = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
    using LoadKeyboardLayoutA_t = HKL(WINAPI*)(LPCSTR, UINT);
    using ActivateKeyboardLayout_t = HKL(WINAPI*)(HKL, UINT);
    SetWindowsHookExA_t oSetWindowsHookExA = nullptr, oSetWindowsHookExW = nullptr;
    LoadKeyboardLayoutA_t oLoadKeyboardLayoutA = nullptr;
    ActivateKeyboardLayout_t oActivateKeyboardLayout = nullptr;
    HOOKPROC gGameKbdLL = nullptr, gGameMouseLL = nullptr;

    bool GameInForeground()
    {
        HWND fg = GetForegroundWindow();
        if (!fg) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid == GetCurrentProcessId();
    }

    LRESULT CALLBACK KbdLLProc(int code, WPARAM wp, LPARAM lp)
    {
        if (code >= 0 && !GameInForeground()) return CallNextHookEx(nullptr, code, wp, lp);
        return gGameKbdLL(code, wp, lp);
    }

    LRESULT CALLBACK MouseLLProc(int code, WPARAM wp, LPARAM lp)
    {
        if (code >= 0 && !GameInForeground()) return CallNextHookEx(nullptr, code, wp, lp);
        return gGameMouseLL(code, wp, lp);
    }

    HHOOK WrapHook(SetWindowsHookExA_t orig, int id, HOOKPROC fn, HINSTANCE mod, DWORD tid, const char* api)
    {
        Log("%s(id %d, thread %lu)", api, id, (unsigned long)tid);
        if (cfg.keyboardFix && fn)
        {
            if (id == WH_KEYBOARD_LL && (!gGameKbdLL || gGameKbdLL == fn)) { gGameKbdLL = fn; fn = &KbdLLProc; Log("  keyboard hook limited to when the game is in the foreground"); }
            else if (id == WH_MOUSE_LL && (!gGameMouseLL || gGameMouseLL == fn)) { gGameMouseLL = fn; fn = &MouseLLProc; Log("  mouse hook limited to when the game is in the foreground"); }
        }
        return orig(id, fn, mod, tid);
    }
    HHOOK WINAPI hkSetWindowsHookExA(int id, HOOKPROC fn, HINSTANCE mod, DWORD tid) { return WrapHook(oSetWindowsHookExA, id, fn, mod, tid, "SetWindowsHookExA"); }
    HHOOK WINAPI hkSetWindowsHookExW(int id, HOOKPROC fn, HINSTANCE mod, DWORD tid) { return WrapHook(oSetWindowsHookExW, id, fn, mod, tid, "SetWindowsHookExW"); }

    HKL WINAPI hkLoadKeyboardLayoutA(LPCSTR klid, UINT flags)
    {
        UINT f = flags;
        if (cfg.keyboardFix) f = (flags & ~(KLF_ACTIVATE | KLF_SETFORPROCESS | KLF_REORDER)) | KLF_NOTELLSHELL;
        HKL h = oLoadKeyboardLayoutA(klid, f);
        Log("LoadKeyboardLayoutA(%s, 0x%X -> 0x%X) = %p", klid ? klid : "null", flags, f, (void*)h);
        return h;
    }

    HKL WINAPI hkActivateKeyboardLayout(HKL hkl, UINT flags)
    {
        static int n = 0;
        if (n++ < 10) Log("ActivateKeyboardLayout(%p, 0x%X)%s", (void*)hkl, flags, cfg.keyboardFix ? " blocked" : "");
        if (cfg.keyboardFix) return GetKeyboardLayout(0);
        return oActivateKeyboardLayout(hkl, flags);
    }

    // The game reads the keyboard and mouse through DirectInput in background mode and translates keys with
    // ToAsciiEx/ToUnicodeEx even when another program is in front, which also eats Windows' dead-key state
    // (accents). While the game is not in the foreground: translation calls do nothing, and DirectInput
    // reports no keys/buttons pressed.
    using ToAsciiEx_t = int(WINAPI*)(UINT, UINT, const BYTE*, LPWORD, UINT, HKL);
    using ToUnicodeEx_t = int(WINAPI*)(UINT, UINT, const BYTE*, LPWSTR, int, UINT, HKL);
    ToAsciiEx_t oToAsciiEx = nullptr;
    ToUnicodeEx_t oToUnicodeEx = nullptr;
    int WINAPI hkToAsciiEx(UINT vk, UINT sc, const BYTE* ks, LPWORD out, UINT fl, HKL hkl)
    {
        if (cfg.keyboardFix && !GameInForeground()) return 0;
        return oToAsciiEx(vk, sc, ks, out, fl, hkl);
    }
    int WINAPI hkToUnicodeEx(UINT vk, UINT sc, const BYTE* ks, LPWSTR out, int n, UINT fl, HKL hkl)
    {
        if (cfg.keyboardFix && !GameInForeground()) return 0;
        return oToUnicodeEx(vk, sc, ks, out, n, fl, hkl);
    }

    using DI8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    using DICreateDevice_t = HRESULT(WINAPI*)(void*, REFGUID, void**, LPUNKNOWN);
    using DIGetDeviceState_t = HRESULT(WINAPI*)(void*, DWORD, LPVOID);
    using DIGetDeviceData_t = HRESULT(WINAPI*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
    DI8Create_t oDI8Create = nullptr;
    DICreateDevice_t oDICreateDeviceA = nullptr, oDICreateDeviceW = nullptr;
    // Keyboard, mouse and game controllers can be different classes with their own vtables, so the original
    // methods are kept per vtable (a single global would send a keyboard call to a controller's method and crash).
    struct DIVtblOrig { void** vtbl; DIGetDeviceState_t state; DIGetDeviceData_t data; };
    DIVtblOrig gDIOrig[16] = {};
    SRWLOCK gDILock = SRWLOCK_INIT;
    DIVtblOrig* FindDIOrig(void* dev)
    {
        void** vtbl = *(void***)dev;
        AcquireSRWLockShared(&gDILock);
        DIVtblOrig* r = nullptr;
        for (auto& e : gDIOrig) if (e.vtbl == vtbl) { r = &e; break; }
        ReleaseSRWLockShared(&gDILock);
        return r;
    }

    HRESULT WINAPI hkGetDeviceState(void* dev, DWORD cb, LPVOID data)
    {
        DIVtblOrig* e = FindDIOrig(dev);
        if (!e || !e->state) return DIERR_GENERIC;
        HRESULT hr = e->state(dev, cb, data);
        if (cfg.keyboardFix && data && !GameInForeground())
        {
            memset(data, 0, cb);
            return DI_OK;
        }
        return hr;
    }
    HRESULT WINAPI hkGetDeviceData(void* dev, DWORD cb, LPDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD fl)
    {
        DIVtblOrig* e = FindDIOrig(dev);
        if (!e || !e->data) return DIERR_GENERIC;
        HRESULT hr = e->data(dev, cb, data, inout, fl);
        if (cfg.keyboardFix && inout && !GameInForeground() && !(fl & DIGDD_PEEK))
        {
            *inout = 0;
            return DI_OK;
        }
        return hr;
    }
    void PatchDIDevice(void* dev, bool)
    {
        // IDirectInputDevice8: 9 = GetDeviceState, 10 = GetDeviceData
        void** vtbl = *(void***)dev;
        AcquireSRWLockExclusive(&gDILock);
        bool known = false, added = false;
        for (auto& e : gDIOrig) if (e.vtbl == vtbl) { known = true; break; }
        if (!known)
            for (auto& e : gDIOrig)
                if (!e.vtbl)
                {
                    // record the originals before the vtable points at the hooks
                    e.state = (DIGetDeviceState_t)vtbl[9];
                    e.data = (DIGetDeviceData_t)vtbl[10];
                    e.vtbl = vtbl;
                    added = true;
                    break;
                }
        ReleaseSRWLockExclusive(&gDILock);
        if (added)
        {
            PatchVtbl(dev, 9, (void*)&hkGetDeviceState);
            PatchVtbl(dev, 10, (void*)&hkGetDeviceData);
            Log("DirectInput device class %p hooked", (void*)vtbl);
        }
    }
    HRESULT WINAPI hkDICreateDeviceA(void* di, REFGUID g, void** out, LPUNKNOWN u)
    {
        HRESULT hr = oDICreateDeviceA(di, g, out, u);
        if (SUCCEEDED(hr) && out && *out) { PatchDIDevice(*out, false); static int n = 0; if (n++ < 6) Log("DirectInput device created (A): input blocked while the game is in the background"); }
        return hr;
    }
    HRESULT WINAPI hkDICreateDeviceW(void* di, REFGUID g, void** out, LPUNKNOWN u)
    {
        HRESULT hr = oDICreateDeviceW(di, g, out, u);
        if (SUCCEEDED(hr) && out && *out) { PatchDIDevice(*out, true); static int n = 0; if (n++ < 6) Log("DirectInput device created (W): input blocked while the game is in the background"); }
        return hr;
    }
    HRESULT WINAPI hkDI8Create(HINSTANCE inst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN u)
    {
        HRESULT hr = oDI8Create(inst, ver, riid, out, u);
        if (SUCCEEDED(hr) && out && *out && cfg.keyboardFix)
        {
            bool wide = IsEqualIID(riid, IID_IDirectInput8W) != 0;
            // IDirectInput8: 3 = CreateDevice
            if (wide) { if (auto o = PatchVtbl(*out, 3, (void*)&hkDICreateDeviceW)) oDICreateDeviceW = (DICreateDevice_t)o; }
            else { if (auto o = PatchVtbl(*out, 3, (void*)&hkDICreateDeviceA)) oDICreateDeviceA = (DICreateDevice_t)o; }
            Log("DirectInput8Create (%s)", wide ? "W" : "A");
        }
        return hr;
    }

    // ---------------------------------------------------------------- installed mods overlay (ModListKey, default F11)
    std::vector<std::wstring> gModLines;
    bool gOverlayOn = false;

    std::wstring FileVersion(const std::wstring& path)
    {
        DWORD h = 0, n = GetFileVersionInfoSizeW(path.c_str(), &h);
        if (!n) return L"";
        std::vector<BYTE> b(n);
        VS_FIXEDFILEINFO* fi = nullptr; UINT len = 0;
        if (!GetFileVersionInfoW(path.c_str(), 0, n, b.data()) || !VerQueryValueW(b.data(), L"\\", (void**)&fi, &len) || !fi) return L"";
        wchar_t v[64];
        swprintf_s(v, L"  v%u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
        return v;
    }

    void ScanDir(const std::wstring& rel, const wchar_t* pattern, bool dirs, bool version, std::vector<std::wstring>& out)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((gDir + rel + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (fd.cFileName[0] == L'.' || isDir != dirs) continue;
            std::wstring line = L"   " + std::wstring(fd.cFileName);
            if (version) line += FileVersion(gDir + rel + fd.cFileName);
            out.push_back(line);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    void BuildModList()
    {
        gModLines.clear();
        gModLines.push_back(L"Installed mods   (" + std::wstring(L"F11 or ModListKey to close)"));
        std::vector<std::wstring> v;
        ScanDir(L"plugins\\", L"*.asi", false, true, v);
        ScanDir(L"", L"*.asi", false, true, v);
        ScanDir(L"scripts\\", L"*.asi", false, true, v);
        gModLines.push_back(L"ASI plugins: " + std::to_wstring(v.size()));
        gModLines.insert(gModLines.end(), v.begin(), v.end());
        v.clear();
        ScanDir(L"plugins\\", L"*.off", false, false, v);
        ScanDir(L"plugins\\", L"*.disabled", false, false, v);
        if (!v.empty()) { gModLines.push_back(L"Disabled: " + std::to_wstring(v.size())); gModLines.insert(gModLines.end(), v.begin(), v.end()); }
        v.clear();
        ScanDir(L"update\\", L"*", true, false, v);
        gModLines.push_back(L"update folder (file replacements): " + std::to_wstring(v.size()));
        gModLines.insert(gModLines.end(), v.begin(), v.end());
        v.clear();
        ScanDir(L"", L"*.dll", false, true, v);
        gModLines.push_back(L"DLLs next to GTAIV.exe:");
        gModLines.insert(gModLines.end(), v.begin(), v.end());
    }

    struct Overlay
    {
        IDirect3DTexture9* tex = nullptr;
        UINT w = 0, h = 0; float scale = 0.0f;
        bool keyDown = false;

        void Release() { if (tex) { tex->Release(); tex = nullptr; } }

        bool Build(IDirect3DDevice9* dev, float sc)
        {
            Release();
            if (gModLines.empty()) BuildModList();
            std::wstring text;
            for (auto& l : gModLines) { text += l; text += L"\r\n"; }
            HDC dc = CreateCompatibleDC(nullptr);
            HFONT font = CreateFontW(-(int)(18 * sc), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
            HGDIOBJ oldF = SelectObject(dc, font);
            RECT r = { 0, 0, 4000, 4000 };
            DrawTextW(dc, text.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX);
            int pad = (int)(14 * sc);
            w = r.right + 2 * pad; h = r.bottom + 2 * pad;
            BITMAPINFO bi = {}; bi.bmiHeader = { sizeof(BITMAPINFOHEADER), (LONG)w, -(LONG)h, 1, 32, BI_RGB };
            void* bits = nullptr;
            HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            bool ok = false;
            if (bmp && bits)
            {
                HGDIOBJ oldB = SelectObject(dc, bmp);
                memset(bits, 0, (size_t)w * h * 4);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                RECT tr = { pad, pad, (LONG)w - pad, (LONG)h - pad };
                DrawTextW(dc, text.c_str(), -1, &tr, DT_NOPREFIX);
                GdiFlush();
                if (SUCCEEDED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
                {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0)))
                    {
                        for (UINT y = 0; y < h; y++)
                        {
                            const DWORD* src = (const DWORD*)bits + (size_t)y * w;
                            DWORD* dst = (DWORD*)((BYTE*)lr.pBits + (size_t)y * lr.Pitch);
                            for (UINT x = 0; x < w; x++)
                            {
                                DWORD l = (src[x] >> 8) & 0xFF;            // text coverage (green)
                                DWORD a = 190 + l * 65 / 255;                // dark translucent panel + opaque text
                                DWORD c = l;
                                dst[x] = (a << 24) | (c << 16) | (c << 8) | c;
                            }
                        }
                        tex->UnlockRect(0);
                        ok = true;
                    }
                }
                SelectObject(dc, oldB);
                DeleteObject(bmp);
            }
            SelectObject(dc, oldF);
            DeleteObject(font);
            DeleteDC(dc);
            scale = sc;
            if (!ok) Release();
            return ok;
        }

        void Run(IDirect3DDevice9* dev, IDirect3DStateBlock9* sb)
        {
            if (!cfg.overlayKey) return;
            bool down = GameInForeground() && (GetAsyncKeyState(cfg.overlayKey) & 0x8000);
            if (down && !keyDown) { gOverlayOn = !gOverlayOn; if (gOverlayOn) BuildModList(), Release(); }
            keyDown = down;
            if (!gOverlayOn || !sb) return;

            IDirect3DSurface9* bb = nullptr;
            if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
            D3DSURFACE_DESC d{}; bb->GetDesc(&d);
            RECT cr = {}; float sc = 1.0f;
            if (gWnd && GetClientRect(gWnd, &cr) && cr.right > 0) sc = std::max(1.0f, (float)d.Width / cr.right);
            if (!tex || std::fabs(scale - sc) > 0.01f) Build(dev, sc);
            if (!tex) { bb->Release(); return; }

            sb->Capture();
            IDirect3DSurface9* oldRT = nullptr; dev->GetRenderTarget(0, &oldRT);
            dev->BeginScene();
            dev->SetRenderTarget(0, bb);
            dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            dev->SetTexture(0, tex); dev->SetTexture(1, nullptr);
            dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0); dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
            dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE); dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
            const DWORD rs[][2] = {
                { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE }, { D3DRS_ALPHATESTENABLE, FALSE },
                { D3DRS_ALPHABLENDENABLE, TRUE }, { D3DRS_SRCBLEND, D3DBLEND_SRCALPHA }, { D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA }, { D3DRS_BLENDOP, D3DBLENDOP_ADD },
                { D3DRS_SEPARATEALPHABLENDENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE }, { D3DRS_COLORWRITEENABLE, 0xF }, { D3DRS_SRGBWRITEENABLE, FALSE },
                { D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_FOGENABLE, FALSE }, { D3DRS_LIGHTING, FALSE }, { D3DRS_CLIPPLANEENABLE, 0 }, { D3DRS_FILLMODE, D3DFILL_SOLID },
            };
            for (auto& s : rs) dev->SetRenderState((D3DRENDERSTATETYPE)s[0], s[1]);
            struct V { float x, y, z, rhw, u, v; };
            float x0 = 40.0f * sc - 0.5f, y0 = 40.0f * sc - 0.5f, x1 = x0 + w, y1 = y0 + h;
            V q[4] = { { x0, y0, 0, 1, 0, 0 }, { x1, y0, 0, 1, 1, 0 }, { x0, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
            dev->EndScene();
            dev->SetTexture(0, nullptr);
            if (oldRT) { dev->SetRenderTarget(0, oldRT); oldRT->Release(); }
            sb->Apply();
            bb->Release();
        }
    } gOverlay;
    IDirect3DStateBlock9* gOverlaySB = nullptr;

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
        // Exclusive fullscreen is always turned into a borderless window (not only for resolutions above the
        // monitor): switching between exclusive fullscreen and a window when going back to the monitor
        // resolution crashed the game, and it also keeps alt-tab clean.
        gForcedWindowed = false;
        if (!pp->Windowed && cfg.forceWindowed)
        {
            pp->Windowed = TRUE;
            pp->FullScreen_RefreshRateInHz = 0;
            gForcedWindowed = true;
            Log("%s: exclusive fullscreen turned into a borderless window", what);
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

    void PerFrame(IDirect3DDevice9* dev, HWND wnd, const char* path)
    {
        static const char* logged = nullptr;
        if (logged != path) { logged = path; Log("frames presented through %s", path); }
        if (!gOverlaySB) dev->CreateStateBlock(D3DSBT_ALL, &gOverlaySB);
        gOverlay.Run(dev, gOverlaySB);
        gFilter.Run(dev, wnd);
    }

    // The game presents through IDirect3DSwapChain9::Present (not IDirect3DDevice9::Present), so both are hooked.
    using SCPresent_t = HRESULT(WINAPI*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
    SCPresent_t oSCPresent = nullptr;
    bool gInSCPresent = false;
    HRESULT WINAPI hkSCPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
    {
        IDirect3DDevice9* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(&dev)) && dev)
        {
            PerFrame(dev, wnd, "IDirect3DSwapChain9::Present");
            dev->Release();
        }
        gInSCPresent = true;
        HRESULT hr = oSCPresent(sc, src, dst, wnd, dirty, flags);
        gInSCPresent = false;
        return hr;
    }

    HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
    {
        if (!gInSCPresent) PerFrame(dev, wnd, "IDirect3DDevice9::Present");
        return oPresent(dev, src, dst, wnd, dirty);
    }


    HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
    {
        gFilter.ReleaseDefault();
        if (gOverlaySB) { gOverlaySB->Release(); gOverlaySB = nullptr; }
        PrepareParams(pp, "Reset");
        Subclass();
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
            IDirect3DSwapChain9* sc = nullptr;
            if (SUCCEEDED((*out)->GetSwapChain(0, &sc)) && sc)
            {
                if (auto o = PatchVtbl(sc, 3, (void*)&hkSCPresent)) oSCPresent = (SCPresent_t)o;
                sc->Release();
            }
            Subclass();
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
        if (cfg.log)
            AddVectoredExceptionHandler(0, &CrashHandler);
        if (!cfg.enabled) { Log("disabled in the ini"); return; }
        if (MH_Initialize() != MH_OK) { Log("MinHook init failed"); return; }
        if (!GetModuleHandleW(L"d3d9.dll")) LoadLibraryW(L"d3d9.dll");
        MH_STATUS s = MH_CreateHookApi(L"d3d9.dll", "Direct3DCreate9", (void*)&hkDirect3DCreate9, (void**)&oDirect3DCreate9);
        if (s != MH_OK) Log("hook Direct3DCreate9 failed (%d)", (int)s);
        MH_CreateHookApi(L"user32.dll", "MoveWindow", (void*)&hkMoveWindow, (void**)&oMoveWindow);
        MH_CreateHookApi(L"user32.dll", "SetWindowPos", (void*)&hkSetWindowPos, (void**)&oSetWindowPos);
        MH_CreateHookApi(L"user32.dll", "ScreenToClient", (void*)&hkScreenToClient, (void**)&oScreenToClient);
        MH_CreateHookApi(L"user32.dll", "SetWindowsHookExA", (void*)&hkSetWindowsHookExA, (void**)&oSetWindowsHookExA);
        MH_CreateHookApi(L"user32.dll", "SetWindowsHookExW", (void*)&hkSetWindowsHookExW, (void**)&oSetWindowsHookExW);
        MH_CreateHookApi(L"user32.dll", "LoadKeyboardLayoutA", (void*)&hkLoadKeyboardLayoutA, (void**)&oLoadKeyboardLayoutA);
        MH_CreateHookApi(L"user32.dll", "ActivateKeyboardLayout", (void*)&hkActivateKeyboardLayout, (void**)&oActivateKeyboardLayout);
        MH_CreateHookApi(L"user32.dll", "ToAsciiEx", (void*)&hkToAsciiEx, (void**)&oToAsciiEx);
        MH_CreateHookApi(L"user32.dll", "ToUnicodeEx", (void*)&hkToUnicodeEx, (void**)&oToUnicodeEx);
        if (!GetModuleHandleW(L"dinput8.dll")) LoadLibraryW(L"dinput8.dll");
        s = MH_CreateHookApi(L"dinput8.dll", "DirectInput8Create", (void*)&hkDI8Create, (void**)&oDI8Create);
        if (s != MH_OK) Log("hook DirectInput8Create failed (%d)", (int)s);
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
