#include <windows.h>
#include <tchar.h>
#include <cmath>
#include <string>

#include "PluginInterface.h"
#include "Notepad_plus_msgs.h"
#include "menuCmdID.h"

// Scintilla message IDs (stable across versions)
#define SCI_UNDO 2176
#define SCI_CUT 2177
#define SCI_COPY 2178
#define SCI_PASTE 2179
#define SCI_REDO 2011
#define SCI_DOCUMENTSTART 2316
#define SCI_DOCUMENTEND 2318

static const TCHAR kPluginName[] = TEXT("MouseGestures");

static NppData g_nppData{};
static FuncItem g_funcItems[1]{};

static HINSTANCE g_hInstance = nullptr;
static HHOOK g_mouseHook = nullptr;

static const wchar_t kOverlayClassName[] = L"MouseGestureOverlay";

struct OverlayState {
    HWND hwnd = nullptr;
    HDC memdc = nullptr;
    HBITMAP membmp = nullptr;
    HGDIOBJ oldbmp = nullptr;
    HPEN pen = nullptr;
    HFONT font = nullptr;
    RECT bounds = {0, 0, 0, 0};
    POINT origin = {0, 0};
    bool visible = false;
};

static OverlayState g_overlay;

enum class GesturePhase {
    None,
    Pending,
    Active,
};

struct GestureState {
    GesturePhase phase = GesturePhase::None;
    POINT start = {0, 0};
    POINT last = {0, 0};
    std::wstring seq;
    HWND targetHwnd = nullptr;
};

static GestureState g_gesture;

static HWND GetActiveScintilla() {
    int which = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return (which == 0) ? g_nppData._scintillaMainHandle : g_nppData._scintillaSecondHandle;
}

static int GetActiveView() {
    int which = 0;
    SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return (which == 0) ? MAIN_VIEW : SUB_VIEW;
}

static int GetViewTypeFromActiveView(int activeView) {
    return (activeView == MAIN_VIEW) ? PRIMARY_VIEW : SECOND_VIEW;
}

static void SwitchTab(int delta) {
    int view = GetActiveView();
    int currentIndex = (int)SendMessage(g_nppData._nppHandle, NPPM_GETCURRENTDOCINDEX, 0, view);
    int count = (int)SendMessage(g_nppData._nppHandle, NPPM_GETNBOPENFILES, 0, GetViewTypeFromActiveView(view));

    if (count <= 0 || currentIndex < 0) {
        return;
    }

    int nextIndex = (currentIndex + delta) % count;
    if (nextIndex < 0) {
        nextIndex += count;
    }

    SendMessage(g_nppData._nppHandle, NPPM_ACTIVATEDOC, view, nextIndex);
}

static void ExecuteGesture(const std::wstring &seq) {
    HWND hSci = GetActiveScintilla();
    if (!hSci) {
        return;
    }

    if (seq == L"L") {
        SwitchTab(-1);
        return;
    }
    if (seq == L"R") {
        SwitchTab(1);
        return;
    }
    if (seq == L"U") {
        SendMessage(hSci, SCI_DOCUMENTSTART, 0, 0);
        return;
    }
    if (seq == L"D") {
        SendMessage(hSci, SCI_DOCUMENTEND, 0, 0);
        return;
    }
    if (seq == L"DR") {
        SendMessage(g_nppData._nppHandle, WM_COMMAND, IDM_FILE_CLOSE, 0);
        return;
    }
    if (seq == L"DL") {
        SendMessage(hSci, SCI_CUT, 0, 0);
        return;
    }
    if (seq == L"UL") {
        SendMessage(hSci, SCI_COPY, 0, 0);
        return;
    }
    if (seq == L"RD") {
        SendMessage(hSci, SCI_PASTE, 0, 0);
        return;
    }
    if (seq == L"LR") {
        SendMessage(hSci, SCI_UNDO, 0, 0);
        return;
    }
    if (seq == L"RL") {
        SendMessage(hSci, SCI_REDO, 0, 0);
        return;
    }
}

static bool IsScintillaWindow(HWND hwnd) {
    if (!hwnd) {
        return false;
    }
    wchar_t className[64] = {0};
    if (!GetClassNameW(hwnd, className, 63)) {
        return false;
    }
    return lstrcmpiW(className, L"Scintilla") == 0;
}

static bool EnsureOverlayResources() {
    if (g_overlay.hwnd) {
        return true;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = g_hInstance;
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_overlay.origin = {x, y};
    g_overlay.bounds = {0, 0, w, h};

    g_overlay.hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kOverlayClassName,
        L"",
        WS_POPUP,
        x,
        y,
        w,
        h,
        GetDesktopWindow(),
        nullptr,
        g_hInstance,
        nullptr);

    if (!g_overlay.hwnd) {
        return false;
    }

    SetLayeredWindowAttributes(g_overlay.hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY);

    HDC screenDC = GetDC(nullptr);
    g_overlay.memdc = CreateCompatibleDC(screenDC);
    g_overlay.membmp = CreateCompatibleBitmap(screenDC, w, h);
    g_overlay.oldbmp = SelectObject(g_overlay.memdc, g_overlay.membmp);
    ReleaseDC(nullptr, screenDC);

    g_overlay.pen = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, 0, &ncm, 0)) {
        ncm.lfMessageFont.lfHeight = -32;
        lstrcpyW(ncm.lfMessageFont.lfFaceName, L"Segoe UI");
    }
    ncm.lfMessageFont.lfHeight = -48;
    g_overlay.font = CreateFontIndirectW(&ncm.lfMessageFont);

    return g_overlay.memdc != nullptr;
}

static void ClearOverlay() {
    if (!g_overlay.memdc) {
        return;
    }
    HBRUSH brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(g_overlay.memdc, &g_overlay.bounds, brush);
}

static void PresentOverlay() {
    if (!g_overlay.hwnd || !g_overlay.memdc) {
        return;
    }
    HDC wndDC = GetDC(g_overlay.hwnd);
    int w = g_overlay.bounds.right - g_overlay.bounds.left;
    int h = g_overlay.bounds.bottom - g_overlay.bounds.top;
    BitBlt(wndDC, 0, 0, w, h, g_overlay.memdc, 0, 0, SRCCOPY);
    ReleaseDC(g_overlay.hwnd, wndDC);
}

static void ShowOverlay() {
    if (!EnsureOverlayResources()) {
        return;
    }
    ClearOverlay();
    ShowWindow(g_overlay.hwnd, SW_SHOWNOACTIVATE);
    g_overlay.visible = true;
}

static void HideOverlay() {
    if (!g_overlay.hwnd) {
        return;
    }
    ShowWindow(g_overlay.hwnd, SW_HIDE);
    g_overlay.visible = false;
}

static void DrawOverlayText(const std::wstring &text) {
    if (!g_overlay.memdc) {
        return;
    }
    RECT textRect = {10, 10, 600, 140};
    HBRUSH brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    FillRect(g_overlay.memdc, &textRect, brush);

    HFONT oldFont = (HFONT)SelectObject(g_overlay.memdc, g_overlay.font);
    SetBkMode(g_overlay.memdc, TRANSPARENT);
    SetTextColor(g_overlay.memdc, RGB(255, 0, 0));
    TextOutW(g_overlay.memdc, 10, 10, text.c_str(), (int)text.size());
    SelectObject(g_overlay.memdc, oldFont);
}

static char DirectionFromDelta(int dx, int dy) {
    const double threshold = 10.0;
    const double adx = (dx < 0) ? -dx : dx;
    const double ady = (dy < 0) ? -dy : dy;
    if (adx < threshold && ady < threshold) {
        return 0;
    }

    // Negate dy to convert screen coords (Y-down) to math coords (Y-up)
    double angle = atan2(-(double)dy, (double)dx) * (180.0 / 3.141592741012573);
    if (angle >= -45.0 && angle <= 45.0) {
        return 'R';
    }
    if (angle > 45.0 && angle < 135.0) {
        return 'U';
    }
    if (angle <= -45.0 && angle >= -135.0) {
        return 'D';
    }
    return 'L';
}

static void AppendDirection(char dir) {
    if (dir == 0) {
        return;
    }
    wchar_t wdir = (wchar_t)dir;
    if (!g_gesture.seq.empty() && g_gesture.seq.back() == wdir) {
        return;
    }
    if (g_gesture.seq.size() >= 2) {
        return;
    }
    g_gesture.seq.push_back(wdir);
}

static void DrawLineSegment(const POINT &from, const POINT &to) {
    if (!g_overlay.memdc || !g_overlay.pen) {
        return;
    }
    HPEN oldPen = (HPEN)SelectObject(g_overlay.memdc, g_overlay.pen);
    MoveToEx(g_overlay.memdc, from.x - g_overlay.origin.x, from.y - g_overlay.origin.y, nullptr);
    LineTo(g_overlay.memdc, to.x - g_overlay.origin.x, to.y - g_overlay.origin.y);
    SelectObject(g_overlay.memdc, oldPen);
}

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code < 0) {
        return CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }

    const MOUSEHOOKSTRUCT *info = reinterpret_cast<const MOUSEHOOKSTRUCT *>(lParam);
    if (!info) {
        return CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }

    switch (wParam) {
    case WM_RBUTTONDOWN: {
        if (!IsScintillaWindow(info->hwnd)) {
            break;
        }
        g_gesture.phase = GesturePhase::Pending;
        g_gesture.start = info->pt;
        g_gesture.last = info->pt;
        g_gesture.seq.clear();
        g_gesture.targetHwnd = info->hwnd;
        return CallNextHookEx(g_mouseHook, code, wParam, lParam);
    }
    case WM_MOUSEMOVE: {
        if (g_gesture.phase == GesturePhase::None) {
            break;
        }

        POINT pt = info->pt;
        int dx = pt.x - g_gesture.last.x;
        int dy = pt.y - g_gesture.last.y;

        if (g_gesture.phase == GesturePhase::Pending) {
            char dir = DirectionFromDelta(dx, dy);
            if (dir == 0) {
                break;
            }
            g_gesture.phase = GesturePhase::Active;
            ShowOverlay();
            DrawLineSegment(g_gesture.start, pt);
            AppendDirection(dir);
            DrawOverlayText(L"Gesture: " + g_gesture.seq);
            PresentOverlay();
            g_gesture.last = pt;
            return 1;
        }

        if (g_gesture.phase == GesturePhase::Active) {
            char dir = DirectionFromDelta(dx, dy);
            if (dir == 0) {
                return 1;
            }
            DrawLineSegment(g_gesture.last, pt);
            AppendDirection(dir);
            DrawOverlayText(L"Gesture: " + g_gesture.seq);
            PresentOverlay();
            g_gesture.last = pt;
            return 1;
        }
        break;
    }
    case WM_RBUTTONUP: {
        if (g_gesture.phase == GesturePhase::Active) {
            HideOverlay();
            ExecuteGesture(g_gesture.seq);
            g_gesture.phase = GesturePhase::None;
            g_gesture.targetHwnd = nullptr;
            g_gesture.seq.clear();
            return 1;
        }
        g_gesture.phase = GesturePhase::None;
        g_gesture.targetHwnd = nullptr;
        g_gesture.seq.clear();
        break;
    }
    default:
        break;
    }

    return CallNextHookEx(g_mouseHook, code, wParam, lParam);
}

static void InstallMouseHook() {
    if (g_mouseHook) {
        return;
    }
    DWORD threadId = GetCurrentThreadId();
    g_mouseHook = SetWindowsHookExW(WH_MOUSE, MouseHookProc, g_hInstance, threadId);
}

static void RemoveMouseHook() {
    if (!g_mouseHook) {
        return;
    }
    UnhookWindowsHookEx(g_mouseHook);
    g_mouseHook = nullptr;
}

static void ShowAbout() {
    const TCHAR *msg =
        TEXT("MouseGestures for Notepad++\r\n\r\n")
        TEXT("Gestures:\r\n")
        TEXT("Left: Previous tab\r\n")
        TEXT("Right: Next tab\r\n")
        TEXT("Up: Top of document\r\n")
        TEXT("Down: Bottom of document\r\n")
        TEXT("Down, Right: Close\r\n")
        TEXT("Down, Left: Cut\r\n")
        TEXT("Up, Left: Copy\r\n")
        TEXT("Right, Down: Paste\r\n")
        TEXT("Left, Right: Undo\r\n")
        TEXT("Right, Left: Redo\r\n");
    MessageBox(g_nppData._nppHandle, msg, TEXT("MouseGestures"), MB_OK | MB_ICONINFORMATION);
}

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData) {
    g_nppData = notepadPlusData;
    InstallMouseHook();
}

extern "C" __declspec(dllexport) const TCHAR *getName() {
    return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *nbF) {
    if (nbF) {
        *nbF = 1;
    }
    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *) {
    // No-op
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) {
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() {
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hModule;
        g_funcItems[0]._pFunc = ShowAbout;
        lstrcpy(g_funcItems[0]._itemName, TEXT("About"));
        g_funcItems[0]._init2Check = false;
        g_funcItems[0]._pShKey = nullptr;
        break;
    case DLL_PROCESS_DETACH:
        RemoveMouseHook();
        if (g_overlay.memdc) {
            SelectObject(g_overlay.memdc, g_overlay.oldbmp);
            DeleteObject(g_overlay.membmp);
            DeleteDC(g_overlay.memdc);
        }
        if (g_overlay.pen) {
            DeleteObject(g_overlay.pen);
        }
        if (g_overlay.font) {
            DeleteObject(g_overlay.font);
        }
        if (g_overlay.hwnd) {
            DestroyWindow(g_overlay.hwnd);
        }
        break;
    default:
        break;
    }
    return TRUE;
}
