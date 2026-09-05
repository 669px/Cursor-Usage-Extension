#include "widget.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <objidl.h>
#ifndef PROPID
typedef ULONG PROPID;
#endif
#include <gdiplus.h>

namespace {

constexpr wchar_t kMutexName[] = L"Local\\CursorUsageWidget.byte4day";
constexpr wchar_t kWakeMsgName[] = L"CursorUsageWidget_Wake_byte4day";

void EnableDpiAwareness() {
  using SetCtxFn = BOOL(WINAPI*)(HANDLE);
  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  auto setCtx = reinterpret_cast<SetCtxFn>(
      GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
  if (setCtx) {
    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
    setCtx(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
    return;
  }
  using SetAwareFn = HRESULT(WINAPI*)(int);
  HMODULE shcore = LoadLibraryW(L"Shcore.dll");
  if (shcore) {
    auto setAware = reinterpret_cast<SetAwareFn>(
        GetProcAddress(shcore, "SetProcessDpiAwareness"));
    if (setAware)
      setAware(2);  // PROCESS_PER_MONITOR_DPI_AWARE
  }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int);

#if defined(__MINGW32__)
int APIENTRY WinMain(HINSTANCE instance, HINSTANCE prev, LPSTR, int show) {
  return wWinMain(instance, prev, GetCommandLineW(), show);
}
#endif

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  EnableDpiAwareness();

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (!mutex)
    return 1;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    UINT wake = RegisterWindowMessageW(kWakeMsgName);
    PostMessageW(HWND_BROADCAST, wake, 0, 0);
    CloseHandle(mutex);
    return 0;
  }

  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  Gdiplus::GdiplusStartupInput gdiplusInput;
  ULONG_PTR gdiplusToken = 0;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);

  int code = 1;
  {
    cu::Widget widget(instance);
    if (widget.Create())
      code = widget.Run();
  }

  Gdiplus::GdiplusShutdown(gdiplusToken);
  if (SUCCEEDED(com))
    CoUninitialize();
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return code;
}
