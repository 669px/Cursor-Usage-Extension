#include "widget.hpp"
#include "util.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <objidl.h>
#ifndef PROPID
typedef ULONG PROPID;
#endif
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace cu {
namespace {

void CopyTip(wchar_t* dest, size_t destChars, const wchar_t* src) {
  if (!dest || destChars == 0)
    return;
  wcsncpy(dest, src ? src : L"", destChars - 1);
  dest[destChars - 1] = L'\0';
}

int WideToInt(const wchar_t* s) {
  return static_cast<int>(wcstol(s, nullptr, 10));
}

int WindowDpi(HWND hwnd) {
  using Fn = UINT(WINAPI*)(HWND);
  static Fn fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn)
    return static_cast<int>(fn(hwnd));
  HDC hdc = GetDC(hwnd);
  int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
  if (hdc)
    ReleaseDC(hwnd, hdc);
  return dpi > 0 ? dpi : 96;
}

constexpr COLORREF kBg = RGB(28, 28, 30);
constexpr COLORREF kBorder = RGB(58, 58, 62);
constexpr COLORREF kTrack = RGB(66, 66, 70);
constexpr COLORREF kText = RGB(236, 236, 240);
constexpr COLORREF kMuted = RGB(148, 148, 154);
constexpr COLORREF kLink = RGB(98, 160, 234);
constexpr COLORREF kLinkHot = RGB(153, 193, 241);
constexpr COLORREF kLow = RGB(51, 209, 122);
constexpr COLORREF kHigh = RGB(255, 120, 0);
constexpr COLORREF kCrit = RGB(224, 27, 36);

COLORREF SeverityColor(double util) {
  if (util >= 90)
    return kCrit;
  if (util >= 75)
    return kHigh;
  return kLow;
}

void DrawRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                   const Gdiplus::Brush& brush) {
  Gdiplus::GraphicsPath path;
  float d = radius * 2.f;
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  path.CloseFigure();
  g.FillPath(&brush, &path);
}

void StrokeRoundRect(Gdiplus::Graphics& g, const Gdiplus::RectF& r, float radius,
                     const Gdiplus::Pen& pen) {
  Gdiplus::GraphicsPath path;
  float d = radius * 2.f;
  path.AddArc(r.X, r.Y, d, d, 180, 90);
  path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  path.CloseFigure();
  g.DrawPath(&pen, &path);
}

Gdiplus::Color ToGp(COLORREF c, BYTE a = 255) {
  return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

bool PtIn(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

struct SettingsState {
  Config* config = nullptr;
  bool applied = false;
  HWND refreshEdit = nullptr;
  HWND billingCheck = nullptr;
  HWND tierCheck = nullptr;
  HWND remainingCheck = nullptr;
  HWND compactCheck = nullptr;
  HWND hiddenCheck = nullptr;
  HWND cursorCheck = nullptr;
  HWND claudeCheck = nullptr;
  HWND codexCheck = nullptr;
  HWND providerCombo = nullptr;
  HWND poolCombo = nullptr;
  HWND opacityEdit = nullptr;
  HWND proxyEdit = nullptr;
};

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_CREATE: {
      auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      state = static_cast<SettingsState*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

      int y = 16;
      auto label = [&](const wchar_t* text) {
        CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, 16, y, 280, 18, hwnd, nullptr, nullptr,
                      nullptr);
        y += 20;
      };
      auto check = [&](HWND& out, int id, const wchar_t* text, bool on) {
        out = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16,
                            y, 280, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            nullptr, nullptr);
        SendMessageW(out, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 26;
      };

      label(L"Refresh interval (seconds)");
      state->refreshEdit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(state->config->refreshIntervalSec).c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 16, y, 90, 24, hwnd,
          reinterpret_cast<HMENU>(100), nullptr, nullptr);
      y += 34;

      check(state->billingCheck, 101, L"Show billing / credits", state->config->showBilling);
      check(state->tierCheck, 102, L"Show plan tier", state->config->showTier);
      check(state->remainingCheck, 103, L"Show remaining percentage",
            state->config->usageDisplay == "remaining");
      check(state->cursorCheck, 110, L"Show Cursor", state->config->showCursor);
      check(state->claudeCheck, 111, L"Show Claude", state->config->showClaude);
      check(state->codexCheck, 112, L"Show Codex", state->config->showCodex);
      check(state->compactCheck, 106, L"Start in compact mode", state->config->startCompact);
      check(state->hiddenCheck, 107, L"Start hidden in tray", state->config->startHidden);

      label(L"Panel provider");
      state->providerCombo = CreateWindowW(
          L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 16, y, 220,
          120, hwnd, reinterpret_cast<HMENU>(109), nullptr, nullptr);
      SendMessageW(state->providerCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Most used"));
      SendMessageW(state->providerCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cursor"));
      SendMessageW(state->providerCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Claude"));
      SendMessageW(state->providerCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Codex"));
      int psel = 0;
      if (state->config->panelProvider == "cursor")
        psel = 1;
      else if (state->config->panelProvider == "claude")
        psel = 2;
      else if (state->config->panelProvider == "codex")
        psel = 3;
      SendMessageW(state->providerCombo, CB_SETCURSEL, psel, 0);
      y += 34;

      label(L"Panel pool");
      state->poolCombo = CreateWindowW(
          L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 16, y, 220,
          120, hwnd, reinterpret_cast<HMENU>(104), nullptr, nullptr);
      SendMessageW(state->poolCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Most used"));
      SendMessageW(state->poolCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Primary / Auto / 5h"));
      SendMessageW(state->poolCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Secondary / API / 7d"));
      SendMessageW(state->poolCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Total"));
      int sel = 0;
      if (state->config->panelWindow == "auto")
        sel = 1;
      else if (state->config->panelWindow == "api")
        sel = 2;
      else if (state->config->panelWindow == "total")
        sel = 3;
      SendMessageW(state->poolCombo, CB_SETCURSEL, sel, 0);
      y += 34;

      label(L"Opacity (80–255)");
      state->opacityEdit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(state->config->opacity).c_str(),
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER, 16, y, 90, 24, hwnd,
          reinterpret_cast<HMENU>(108), nullptr, nullptr);
      y += 34;

      label(L"Proxy URL (optional)");
      state->proxyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->config->proxyUrl.c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, y,
                                         290, 24, hwnd, reinterpret_cast<HMENU>(105), nullptr,
                                         nullptr);
      y += 40;

      CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 150, y,
                    74, 28, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
      CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 232, y, 74, 28, hwnd,
                    reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK && state) {
        wchar_t buf[64];
        GetWindowTextW(state->refreshEdit, buf, 64);
        int v = WideToInt(buf);
        state->config->refreshIntervalSec = (std::max)(10, v > 0 ? v : 300);
        state->config->showBilling =
            SendMessageW(state->billingCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->showTier = SendMessageW(state->tierCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->usageDisplay =
            SendMessageW(state->remainingCheck, BM_GETCHECK, 0, 0) == BST_CHECKED ? "remaining"
                                                                                  : "used";
        state->config->startCompact =
            SendMessageW(state->compactCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->startHidden =
            SendMessageW(state->hiddenCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->showCursor =
            SendMessageW(state->cursorCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->showClaude =
            SendMessageW(state->claudeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        state->config->showCodex =
            SendMessageW(state->codexCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        int psel = static_cast<int>(SendMessageW(state->providerCombo, CB_GETCURSEL, 0, 0));
        const char* providers[] = {"max", "cursor", "claude", "codex"};
        if (psel >= 0 && psel < 4)
          state->config->panelProvider = providers[psel];
        int sel = static_cast<int>(SendMessageW(state->poolCombo, CB_GETCURSEL, 0, 0));
        const char* pools[] = {"max", "auto", "api", "total"};
        if (sel >= 0 && sel < 4)
          state->config->panelWindow = pools[sel];
        GetWindowTextW(state->opacityEdit, buf, 64);
        int o = WideToInt(buf);
        state->config->opacity = o < 80 ? 80 : (o > 255 ? 255 : o);
        wchar_t proxy[512];
        GetWindowTextW(state->proxyEdit, proxy, 512);
        state->config->proxyUrl = proxy;
        state->applied = true;
        DestroyWindow(hwnd);
        return 0;
      }
      if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDCLOSE) {
        DestroyWindow(hwnd);
        return 0;
      }
      break;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

Widget::Widget(HINSTANCE instance) : instance_(instance) {
  config_.Load();
  expanded_ = !config_.startCompact;
  taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
  wakeMsg_ = RegisterWindowMessageW(L"CursorUsageWidget_Wake_669px");
}

Widget::~Widget() {
  DestroyTray();
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

int Widget::S(int v) const {
  return MulDiv(v, dpi_, 96);
}

int Widget::Width() const {
  return S(expanded_ ? kExpandedW : kCompactW);
}

int Widget::ExpandedContentHeight() const {
  // Header + footer + per enabled provider (title + meters + optional billing)
  int h = 52 + 40;  // header summary + footer
  auto addProvider = [&](bool enabled, const ProviderUsage& p) {
    if (!enabled)
      return;
    h += 18;  // title
    h += 42;  // meter A
    if (!(p.ok && p.b.missing))
      h += 42;  // meter B
    if (config_.showBilling && p.ok && !p.billing.empty())
      h += 14;
    h += 8;  // gap
  };
  addProvider(config_.showCursor, usage_.cursor);
  addProvider(config_.showClaude, usage_.claude);
  addProvider(config_.showCodex, usage_.codex);
  if (!config_.showCursor && !config_.showClaude && !config_.showCodex)
    h += 40;
  return (std::max)(kExpandedHMin, h);
}

int Widget::Height() const {
  return S(expanded_ ? ExpandedContentHeight() : kCompactH);
}

void Widget::ApplyShape() {
  HRGN rgn = CreateRoundRectRgn(0, 0, Width() + 1, Height() + 1, S(14), S(14));
  SetWindowRgn(hwnd_, rgn, TRUE);
}

void Widget::ApplyOpacity() {
  SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(config_.opacity), LWA_ALPHA);
}

void Widget::Relayout(bool keepTopLeft) {
  RECT rc{};
  GetWindowRect(hwnd_, &rc);
  int x = rc.left;
  int y = rc.top;
  (void)keepTopLeft;
  SetWindowPos(hwnd_, nullptr, x, y, Width(), Height(), SWP_NOZORDER | SWP_NOACTIVATE);
  ApplyShape();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Widget::Create() {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = instance_;
  wc.lpszClassName = L"CursorUsageWidget";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  DWORD ex = WS_EX_TOOLWINDOW | WS_EX_LAYERED;
  if (config_.alwaysOnTop)
    ex |= WS_EX_TOPMOST;

  int x = config_.windowX >= 0 ? config_.windowX : CW_USEDEFAULT;
  int y = config_.windowY >= 0 ? config_.windowY : CW_USEDEFAULT;

  hwnd_ = CreateWindowExW(ex, wc.lpszClassName, L"Cursor Usage", WS_POPUP, x, y, 100, 40, nullptr,
                          nullptr, instance_, this);
  if (!hwnd_)
    return false;

  dpi_ = WindowDpi(hwnd_);
  if (dpi_ <= 0)
    dpi_ = 96;

  SetWindowPos(hwnd_, nullptr, 0, 0, Width(), Height(), SWP_NOMOVE | SWP_NOZORDER);
  ApplyOpacity();
  ApplyShape();

  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));

  CreateTray();
  SetTimer(hwnd_, kTimerRefresh, static_cast<UINT>(config_.refreshIntervalSec) * 1000, nullptr);
  ScheduleCountdown();
  RefreshAsync();
  return true;
}

void Widget::Show(bool visible) {
  ShowWindow(hwnd_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

int Widget::Run() {
  Show(!config_.startHidden);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK Widget::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  Widget* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<Widget*>(cs->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<Widget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (!self)
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  return self->HandleMessage(msg, wParam, lParam);
}

double Widget::DisplayPct(double util) const {
  return config_.usageDisplay == "remaining" ? 100.0 - util : util;
}

std::wstring Widget::SummaryLabel() const {
  const ProviderUsage* snap = SelectProvider(usage_, config_);
  if (!snap)
    return error_.empty() ? L"..." : Utf8ToWide(error_);
  if (snap->isUnlimited)
    return L"∞";
  const Pool* p = SelectPool(*snap, config_.panelWindow);
  if (!p)
    return L"-";
  return std::to_wstring(static_cast<int>(std::lround(DisplayPct(p->utilization)))) + L"%";
}

COLORREF Widget::SummaryColor() const {
  const ProviderUsage* snap = SelectProvider(usage_, config_);
  if (!snap)
    return kHigh;
  if (snap->isUnlimited)
    return kLow;
  const Pool* p = SelectPool(*snap, config_.panelWindow);
  return SeverityColor(p ? p->utilization : 0);
}

RECT Widget::LinkRect(int index) const {
  const int w = S(52);
  const int h = S(18);
  const int y = Height() - S(28);
  const int right = Width() - S(14);
  return RECT{right - (3 - index) * (w + S(8)) - w, y, right - (3 - index) * (w + S(8)), y + h};
}

RECT Widget::ChromeRect(bool hide) const {
  const int size = S(18);
  const int top = S(10);
  const int right = Width() - S(10);
  if (hide)
    return RECT{right - size, top, right, top + size};
  return RECT{right - size * 2 - S(4), top, right - size - S(4), top + size};
}

Widget::Hit Widget::HitTest(int x, int y) const {
  if (PtIn(ChromeRect(true), x, y))
    return Hit::Hide;
  if (PtIn(ChromeRect(false), x, y))
    return Hit::ToggleExpand;
  if (expanded_) {
    if (PtIn(LinkRect(0), x, y))
      return Hit::Refresh;
    if (PtIn(LinkRect(1), x, y))
      return Hit::Open;
    if (PtIn(LinkRect(2), x, y))
      return Hit::Prefs;
  }
  return Hit::None;
}

void Widget::SetExpanded(bool expanded) {
  if (expanded_ == expanded)
    return;
  expanded_ = expanded;
  Relayout(true);
}

void Widget::SnapToEdges(int& x, int& y) const {
  HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(mon, &mi))
    return;
  const int snap = S(12);
  const RECT& wa = mi.rcWork;
  if (std::abs(x - wa.left) < snap)
    x = wa.left + S(8);
  if (std::abs((x + Width()) - wa.right) < snap)
    x = wa.right - Width() - S(8);
  if (std::abs(y - wa.top) < snap)
    y = wa.top + S(8);
  if (std::abs((y + Height()) - wa.bottom) < snap)
    y = wa.bottom - Height() - S(8);
}

LRESULT Widget::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == taskbarCreated_) {
    trayAdded_ = false;
    CreateTray();
    return 0;
  }
  if (wakeMsg_ && msg == wakeMsg_) {
    Show(true);
    SetForegroundWindow(hwnd_);
    SetExpanded(true);
    return 0;
  }

  switch (msg) {
    case WM_DPICHANGED: {
      dpi_ = HIWORD(wParam);
      auto* tip = reinterpret_cast<RECT*>(lParam);
      SetWindowPos(hwnd_, nullptr, tip->left, tip->top, Width(), Height(),
                   SWP_NOZORDER | SWP_NOACTIVATE);
      ApplyShape();
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd_, &ps);
      Paint(hdc);
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR: {
      if (LOWORD(lParam) == HTCLIENT) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        Hit hit = HitTest(pt.x, pt.y);
        if (hit != Hit::None) {
          SetCursor(LoadCursor(nullptr, IDC_HAND));
          return TRUE;
        }
      }
      break;
    }
    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&tme);
      if (dragging_) {
        POINT cur{};
        GetCursorPos(&cur);
        if (std::abs(cur.x - dragStart_.x) > 3 || std::abs(cur.y - dragStart_.y) > 3)
          movedWhileDrag_ = true;
        int nx = windowStart_.x + (cur.x - dragStart_.x);
        int ny = windowStart_.y + (cur.y - dragStart_.y);
        SnapToEdges(nx, ny);
        SetWindowPos(hwnd_, nullptr, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return 0;
      }
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      int hot = static_cast<int>(HitTest(x, y));
      if (hot != hover_) {
        hover_ = hot;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      if (hover_ != -1) {
        hover_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(lParam);
      int y = GET_Y_LPARAM(lParam);
      Hit hit = HitTest(x, y);
      if (hit == Hit::Refresh) {
        RefreshAsync();
        return 0;
      }
      if (hit == Hit::Open) {
        const ProviderUsage* snap = SelectProvider(usage_, config_);
        std::wstring url = L"https://cursor.com/dashboard/usage";
        if (snap && snap->id == "claude")
          url = L"https://claude.ai/settings/usage";
        else if (snap && snap->id == "codex")
          url = L"https://chatgpt.com/codex";
        OpenUrl(url);
        return 0;
      }
      if (hit == Hit::Prefs) {
        OpenSettings();
        return 0;
      }
      if (hit == Hit::Hide) {
        Show(false);
        return 0;
      }
      if (hit == Hit::ToggleExpand) {
        SetExpanded(!expanded_);
        return 0;
      }
      dragging_ = true;
      movedWhileDrag_ = false;
      SetCapture(hwnd_);
      GetCursorPos(&dragStart_);
      RECT rc;
      GetWindowRect(hwnd_, &rc);
      windowStart_ = {rc.left, rc.top};
      return 0;
    }
    case WM_LBUTTONUP:
      if (dragging_) {
        dragging_ = false;
        ReleaseCapture();
        PersistPosition();
        if (!movedWhileDrag_ && !expanded_)
          SetExpanded(true);
      }
      return 0;
    case WM_LBUTTONDBLCLK:
      // Single-click already expands compact; ignore double-click to avoid toggle flicker.
      return 0;
    case WM_RBUTTONUP:
      ShowContextMenu();
      return 0;
    case WM_TIMER:
      if (wParam == kTimerRefresh)
        RefreshAsync();
      else if (wParam == kTimerCountdown) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        ScheduleCountdown();
      }
      return 0;
    case kMsgRefreshDone: {
      auto* result = reinterpret_cast<AllUsage*>(lParam);
      ApplyUsage(*result);
      delete result;
      refreshing_ = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      UpdateTray();
      ScheduleCountdown();
      return 0;
    }
    case kTrayMsg:
      if (lParam == WM_LBUTTONUP || lParam == NIN_SELECT) {
        bool visible = IsWindowVisible(hwnd_);
        Show(!visible);
        if (!visible) {
          SetForegroundWindow(hwnd_);
          SetExpanded(true);
        }
      } else if (lParam == WM_RBUTTONUP) {
        ShowContextMenu();
      }
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case 1001:
          RefreshAsync();
          break;
        case 1002: {
          const ProviderUsage* snap = SelectProvider(usage_, config_);
          std::wstring url = L"https://cursor.com/dashboard/usage";
          if (snap && snap->id == "claude")
            url = L"https://claude.ai/settings/usage";
          else if (snap && snap->id == "codex")
            url = L"https://chatgpt.com/codex";
          OpenUrl(url);
          break;
        }
        case 1003:
          OpenSettings();
          break;
        case 1004:
          Show(!IsWindowVisible(hwnd_));
          break;
        case 1005:
          DestroyWindow(hwnd_);
          break;
        case 1006:
          config_.alwaysOnTop = !config_.alwaysOnTop;
          SetWindowPos(hwnd_, config_.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE);
          config_.Save();
          break;
        case 1007:
          SetExpanded(!expanded_);
          break;
      }
      return 0;
    case WM_DESTROY:
      PersistPosition();
      KillTimer(hwnd_, kTimerRefresh);
      KillTimer(hwnd_, kTimerCountdown);
      DestroyTray();
      hwnd_ = nullptr;
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void Widget::DrawRing(void* graphics, float cx, float cy, float radius, float stroke, double util,
                      bool known) const {
  auto& g = *static_cast<Gdiplus::Graphics*>(graphics);
  Gdiplus::Pen track(ToGp(kText, 40), stroke);
  track.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
  g.DrawArc(&track, cx - radius, cy - radius, radius * 2, radius * 2, 0, 360);

  if (!known || util <= 0)
    return;

  Gdiplus::Pen arc(ToGp(SeverityColor(util)), stroke);
  arc.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
  float sweep = static_cast<float>((ClampPct(util) / 100.0) * 360.0);
  g.DrawArc(&arc, cx - radius, cy - radius, radius * 2, radius * 2, -90, sweep);
}

void Widget::Paint(HDC hdc) {
  const int w = Width();
  const int h = Height();
  HDC mem = CreateCompatibleDC(hdc);
  HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
  HGDIOBJ old = SelectObject(mem, bmp);

  {
    Gdiplus::Graphics g(mem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::SolidBrush bg(ToGp(kBg));
    DrawRoundRect(g, Gdiplus::RectF(0.5f, 0.5f, w - 1.f, h - 1.f), static_cast<float>(S(12)), bg);
    Gdiplus::Pen border(ToGp(kBorder), 1.f);
    StrokeRoundRect(g, Gdiplus::RectF(0.5f, 0.5f, w - 1.f, h - 1.f), static_cast<float>(S(12)),
                    border);

    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font titleFont(&family, static_cast<Gdiplus::REAL>(10.5f * dpi_ / 96.f),
                            Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(&family, static_cast<Gdiplus::REAL>(12.5f * dpi_ / 96.f),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font pctFont(&family, static_cast<Gdiplus::REAL>(13.f * dpi_ / 96.f),
                          Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(&family, static_cast<Gdiplus::REAL>(11.f * dpi_ / 96.f),
                            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font linkFont(&family, static_cast<Gdiplus::REAL>(11.f * dpi_ / 96.f),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    Gdiplus::SolidBrush textBrush(ToGp(kText));
    Gdiplus::SolidBrush mutedBrush(ToGp(kMuted));

    // Chrome: expand / hide
    auto drawChrome = [&](const RECT& r, const wchar_t* glyph, Hit hit) {
      bool hot = hover_ == static_cast<int>(hit);
      Gdiplus::SolidBrush chip(ToGp(hot ? RGB(55, 55, 60) : RGB(40, 40, 44)));
      DrawRoundRect(g,
                    Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                                   static_cast<float>(r.right - r.left),
                                   static_cast<float>(r.bottom - r.top)),
                    static_cast<float>(S(4)), chip);
      Gdiplus::StringFormat center;
      center.SetAlignment(Gdiplus::StringAlignmentCenter);
      center.SetLineAlignment(Gdiplus::StringAlignmentCenter);
      Gdiplus::RectF box(static_cast<float>(r.left), static_cast<float>(r.top),
                         static_cast<float>(r.right - r.left),
                         static_cast<float>(r.bottom - r.top));
      g.DrawString(glyph, -1, &smallFont, box, &center, &mutedBrush);
    };
    drawChrome(ChromeRect(false), expanded_ ? L"▴" : L"▾", Hit::ToggleExpand);
    drawChrome(ChromeRect(true), L"×", Hit::Hide);

    const float ringR = static_cast<float>(S(8));
    const float ringX = static_cast<float>(S(18));
    const float ringY = expanded_ ? static_cast<float>(S(22)) : static_cast<float>(h) * 0.5f;
    const ProviderUsage* snap = SelectProvider(usage_, config_);
    double util = 0;
    bool known = false;
    if (snap && !snap->isUnlimited) {
      if (const Pool* p = SelectPool(*snap, config_.panelWindow)) {
        util = p->utilization;
        known = true;
      }
    }
    DrawRing(&g, ringX, ringY, ringR, static_cast<float>(S(2)), util, known);

    if (!expanded_) {
      auto label = SummaryLabel();
      Gdiplus::SolidBrush pctBrush(ToGp(SummaryColor()));
      Gdiplus::RectF lb;
      g.MeasureString(label.c_str(), -1, &pctFont, Gdiplus::PointF(0, 0), &lb);
      float textX = ringX + ringR + S(8);
      float textY = (static_cast<float>(h) - lb.Height) * 0.5f;
      g.DrawString(label.c_str(), -1, &pctFont, Gdiplus::PointF(textX, textY), &pctBrush);
      if (config_.showTier && snap) {
        auto tier = Utf8ToWide(snap->tier);
        Gdiplus::RectF tb;
        g.MeasureString(tier.c_str(), -1, &smallFont, Gdiplus::PointF(0, 0), &tb);
        float maxRight = static_cast<float>(ChromeRect(false).left) - S(6);
        float tierX = maxRight - tb.Width;
        if (tierX > textX + lb.Width + S(4)) {
          g.DrawString(tier.c_str(), -1, &smallFont,
                       Gdiplus::PointF(tierX, (static_cast<float>(h) - tb.Height) * 0.5f),
                       &mutedBrush);
        }
      }
    } else {
      g.DrawString(L"AI Usage", -1, &titleFont, Gdiplus::PointF(ringX + ringR + S(8), S(10)),
                   &textBrush);
      auto summary = SummaryLabel();
      Gdiplus::SolidBrush pctBrush(ToGp(SummaryColor()));
      g.DrawString(summary.c_str(), -1, &pctFont,
                   Gdiplus::PointF(ringX + ringR + S(8), S(26)), &pctBrush);

      float y = static_cast<float>(S(48));
      const float footerTop = static_cast<float>(h - S(40));

      auto drawMeter = [&](const wchar_t* name, const Pool& pool, bool muted,
                           const std::string& mutedText, bool unlimited) -> bool {
        if (y + S(36) > footerTop)
          return false;
        g.DrawString(name, -1, &bodyFont, Gdiplus::PointF(static_cast<float>(S(14)), y), &textBrush);
        std::wstring right;
        double u = pool.utilization;
        if (muted)
          right = Utf8ToWide(mutedText);
        else if (unlimited) {
          right = L"unlimited";
          u = 0;
        } else if (pool.missing)
          right = L"n/a";
        else
          right = std::to_wstring(static_cast<int>(std::lround(DisplayPct(u)))) +
                  (config_.usageDisplay == "remaining" ? L"% left" : L"% used");
        Gdiplus::RectF rb;
        g.MeasureString(right.c_str(), -1, &bodyFont, Gdiplus::PointF(0, 0), &rb);
        g.DrawString(right.c_str(), -1, &bodyFont,
                     Gdiplus::PointF(static_cast<float>(w - S(14)) - rb.Width, y),
                     muted ? &mutedBrush : &textBrush);
        y += S(16);
        Gdiplus::SolidBrush track(ToGp(kTrack));
        float trackW = static_cast<float>(w - S(28));
        DrawRoundRect(g, Gdiplus::RectF(static_cast<float>(S(14)), y, trackW, static_cast<float>(S(4))),
                      static_cast<float>(S(2)), track);
        if (!muted && !unlimited && !pool.missing) {
          float fillW = static_cast<float>((ClampPct(u) / 100.0) * trackW);
          if (fillW > 0) {
            Gdiplus::SolidBrush fill(ToGp(SeverityColor(u)));
            DrawRoundRect(g,
                          Gdiplus::RectF(static_cast<float>(S(14)), y, fillW,
                                         static_cast<float>(S(4))),
                          static_cast<float>(S(2)), fill);
          }
        }
        y += S(6);
        if (!muted && !pool.missing && !pool.resetsAt.empty()) {
          auto cap = Utf8ToWide(RelativeReset(pool.resetsAt));
          if (!cap.empty() && y + S(12) <= footerTop) {
            g.DrawString(cap.c_str(), -1, &smallFont,
                         Gdiplus::PointF(static_cast<float>(S(14)), y), &mutedBrush);
            y += S(12);
          }
        }
        y += S(4);
        return true;
      };

      auto drawProvider = [&](const wchar_t* title, const ProviderUsage& p, bool enabled,
                              const wchar_t* aName, const wchar_t* bName) {
        if (!enabled || y + S(20) > footerTop)
          return;
        g.DrawString(title, -1, &titleFont, Gdiplus::PointF(static_cast<float>(S(14)), y),
                     &textBrush);
        if (config_.showTier && p.ok) {
          auto tier = Utf8ToWide(p.tier);
          Gdiplus::RectF tb;
          g.MeasureString(tier.c_str(), -1, &smallFont, Gdiplus::PointF(0, 0), &tb);
          g.DrawString(tier.c_str(), -1, &smallFont,
                       Gdiplus::PointF(static_cast<float>(w - S(14)) - tb.Width, y + 1),
                       &mutedBrush);
        }
        y += S(16);
        if (!p.ok) {
          drawMeter(aName, {}, true, p.error.empty() ? "—" : p.error, false);
        } else {
          drawMeter(aName, p.a, false, "", p.isUnlimited);
          if (!p.b.missing)
            drawMeter(bName, p.b, false, "", p.isUnlimited);
          if (config_.showBilling && !p.billing.empty() && y + S(12) <= footerTop) {
            auto billing = Utf8ToWide(p.billing);
            g.DrawString(billing.c_str(), -1, &smallFont,
                         Gdiplus::PointF(static_cast<float>(S(14)), y), &mutedBrush);
            y += S(12);
          }
        }
        y += S(6);
      };

      drawProvider(L"Cursor", usage_.cursor, config_.showCursor, L"Auto", L"API");
      drawProvider(L"Claude", usage_.claude, config_.showClaude, L"5h", L"7d");
      drawProvider(L"Codex", usage_.codex, config_.showCodex, L"Primary", L"Weekly");

      Gdiplus::Pen sep(ToGp(kBorder), 1);
      g.DrawLine(&sep, S(14), h - S(36), w - S(14), h - S(36));

      auto stamp = Utf8ToWide(statusLine_);
      g.DrawString(stamp.c_str(), -1, &smallFont,
                   Gdiplus::PointF(static_cast<float>(S(14)), static_cast<float>(h - S(26))),
                   &mutedBrush);

      const wchar_t* labels[] = {L"Refresh", L"Open", L"Prefs"};
      for (int i = 0; i < 3; ++i) {
        RECT r = LinkRect(i);
        bool hot = hover_ == static_cast<int>(Hit::Refresh) + i;
        Gdiplus::SolidBrush linkBrush(ToGp(hot ? kLinkHot : kLink));
        g.DrawString(labels[i], -1, &linkFont,
                     Gdiplus::PointF(static_cast<float>(r.left), static_cast<float>(r.top)),
                     &linkBrush);
      }
    }
  }

  BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteObject(bmp);
  DeleteDC(mem);
}

void Widget::RefreshAsync() {
  if (refreshing_)
    return;
  refreshing_ = true;
  statusLine_ = "Refreshing...";
  InvalidateRect(hwnd_, nullptr, FALSE);

  HWND hwnd = hwnd_;
  Config cfg = config_;
  std::thread([hwnd, cfg]() {
    auto* result = new AllUsage(FetchAllUsage(cfg));
    if (!IsWindow(hwnd)) {
      delete result;
      return;
    }
    PostMessageW(hwnd, kMsgRefreshDone, 0, reinterpret_cast<LPARAM>(result));
  }).detach();
}

void Widget::ApplyUsage(const AllUsage& result) {
  usage_ = result;
  hasUsage_ = usage_.cursor.ok || usage_.claude.ok || usage_.codex.ok;
  if (!hasUsage_) {
    error_ = usage_.cursor.error;
    if (error_.empty())
      error_ = usage_.claude.error;
    if (error_.empty())
      error_ = usage_.codex.error;
    if (error_.empty())
      error_ = "No providers";
    statusLine_ = "Checked " + FormatTimeNow();
  } else {
    error_.clear();
    statusLine_ = "Updated " + FormatTimeNow();
  }
  if (expanded_)
    Relayout(true);
}

void Widget::ScheduleCountdown() {
  KillTimer(hwnd_, kTimerCountdown);
  UINT ms = 30000;
  if (const ProviderUsage* snap = SelectProvider(usage_, config_)) {
    const Pool* p = SelectPool(*snap, config_.panelWindow);
    if (p && !p->resetsAt.empty()) {
      if (p->resetsAt.rfind("unix:", 0) == 0) {
        try {
          long long sec = std::stoll(p->resetsAt.substr(5));
          auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
          if (sec - now > 0 && sec - now < 90)
            ms = 1000;
        } catch (...) {
        }
      } else {
        auto secs = SecondsUntilIso(p->resetsAt);
        if (secs && *secs > 0 && *secs < 90)
          ms = 1000;
      }
    }
  }
  SetTimer(hwnd_, kTimerCountdown, ms, nullptr);
}

HICON Widget::MakeTrayIcon() {
  const int size = 16;
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = size;
  bmi.bmiHeader.biHeight = -size;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HDC mem = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  if (!dib || !mem) {
    if (dib)
      DeleteObject(dib);
    if (mem)
      DeleteDC(mem);
    return LoadIcon(nullptr, IDI_APPLICATION);
  }

  HGDIOBJ old = SelectObject(mem, dib);
  {
    Gdiplus::Graphics g(mem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    double util = 0;
    bool known = false;
    if (const ProviderUsage* snap = SelectProvider(usage_, config_)) {
      if (!snap->isUnlimited) {
        if (const Pool* p = SelectPool(*snap, config_.panelWindow)) {
          util = p->utilization;
          known = true;
        }
      }
    }
    DrawRing(&g, 8, 8, 5.5f, 2.f, util, known);
    if (!hasUsage_) {
      Gdiplus::Pen p(ToGp(kHigh), 2.f);
      g.DrawArc(&p, 2.5f, 2.5f, 11.f, 11.f, -90, 90);
    }
  }
  SelectObject(mem, old);
  DeleteDC(mem);

  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmColor = dib;
  ii.hbmMask = CreateBitmap(size, size, 1, 1, nullptr);
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(ii.hbmMask);
  DeleteObject(dib);
  return icon ? icon : LoadIcon(nullptr, IDI_APPLICATION);
}

void Widget::CreateTray() {
  if (trayAdded_)
    return;
  if (trayIcon_) {
    DestroyIcon(trayIcon_);
    trayIcon_ = nullptr;
  }
  trayIcon_ = MakeTrayIcon();
  nid_ = {};
  nid_.cbSize = sizeof(nid_);
  nid_.hWnd = hwnd_;
  nid_.uID = 1;
  nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid_.uCallbackMessage = kTrayMsg;
  nid_.hIcon = trayIcon_;
  CopyTip(nid_.szTip, sizeof(nid_.szTip) / sizeof(nid_.szTip[0]), L"AI Usage");
  trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
}

void Widget::DestroyTray() {
  if (trayAdded_) {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    trayAdded_ = false;
  }
  if (trayIcon_) {
    DestroyIcon(trayIcon_);
    trayIcon_ = nullptr;
  }
}

void Widget::UpdateTray() {
  if (!trayAdded_) {
    CreateTray();
    return;
  }
  std::wstring tip = L"AI Usage";
  if (const ProviderUsage* snap = SelectProvider(usage_, config_)) {
    if (snap->isUnlimited) {
      tip = L"AI Usage — unlimited";
    } else if (const Pool* p = SelectPool(*snap, config_.panelWindow)) {
      tip = L"AI Usage — " +
            std::to_wstring(static_cast<int>(std::lround(DisplayPct(p->utilization)))) + L"%";
      if (config_.showTier)
        tip += L" · " + Utf8ToWide(snap->tier);
    }
  } else if (!error_.empty()) {
    tip = L"AI Usage — " + Utf8ToWide(error_);
  }
  if (tip.size() >= 127)
    tip.resize(127);
  CopyTip(nid_.szTip, sizeof(nid_.szTip) / sizeof(nid_.szTip[0]), tip.c_str());
  HICON fresh = MakeTrayIcon();
  if (trayIcon_)
    DestroyIcon(trayIcon_);
  trayIcon_ = fresh;
  nid_.hIcon = trayIcon_;
  Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Widget::ShowContextMenu() {
  POINT pt;
  GetCursorPos(&pt);
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, 1004, IsWindowVisible(hwnd_) ? L"Hide widget" : L"Show widget");
  AppendMenuW(menu, MF_STRING, 1007, expanded_ ? L"Compact view" : L"Expanded view");
  AppendMenuW(menu, MF_STRING | (config_.alwaysOnTop ? MF_CHECKED : 0), 1006, L"Always on top");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 1001, L"Refresh");
  AppendMenuW(menu, MF_STRING, 1002, L"Open dashboard");
  AppendMenuW(menu, MF_STRING, 1003, L"Preferences");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 1005, L"Exit");
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
  DestroyMenu(menu);
}

void Widget::PersistPosition() {
  if (!hwnd_)
    return;
  RECT rc;
  GetWindowRect(hwnd_, &rc);
  config_.windowX = rc.left;
  config_.windowY = rc.top;
  config_.Save();
}

void Widget::OpenSettings() {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = L"CursorUsageSettings";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    registered = true;
  }

  Config copy = config_;
  SettingsState state;
  state.config = &copy;

  HWND dlg =
      CreateWindowExW(WS_EX_DLGMODALFRAME, L"CursorUsageSettings", L"Preferences",
                      WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 360, 680,
                      hwnd_, nullptr, instance_, &state);
  if (!dlg)
    return;

  EnableWindow(hwnd_, FALSE);
  MSG msg;
  while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(dlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  EnableWindow(hwnd_, TRUE);
  SetForegroundWindow(hwnd_);

  if (!state.applied)
    return;

  config_ = copy;
  config_.Save();
  ApplyOpacity();
  KillTimer(hwnd_, kTimerRefresh);
  SetTimer(hwnd_, kTimerRefresh, static_cast<UINT>(config_.refreshIntervalSec) * 1000, nullptr);
  Relayout(true);
  RefreshAsync();
}

}  // namespace cu
