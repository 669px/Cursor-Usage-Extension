#pragma once

#include "config.hpp"
#include "usage.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <guiddef.h>
#include <shellapi.h>

#include <string>

namespace cu {

class Widget {
 public:
  explicit Widget(HINSTANCE instance);
  ~Widget();

  bool Create();
  void Show(bool visible);
  int Run();

 private:
  enum class Hit {
    None,
    Refresh,
    Open,
    Prefs,
    ToggleExpand,
    Hide,
  };

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  int S(int v) const;
  int Width() const;
  int Height() const;
  int ExpandedContentHeight() const;
  void ApplyShape();
  void ApplyOpacity();
  void Relayout(bool keepTopLeft);

  void Paint(HDC hdc);
  void DrawRing(void* graphics, float cx, float cy, float radius, float stroke, double util,
                bool known) const;
  void RefreshAsync();
  void ApplyUsage(const AllUsage& result);
  void ScheduleCountdown();
  void UpdateTray();
  void CreateTray();
  void DestroyTray();
  HICON MakeTrayIcon();
  void ShowContextMenu();
  void OpenSettings();
  void PersistPosition();
  void SetExpanded(bool expanded);
  void SnapToEdges(int& x, int& y) const;

  Hit HitTest(int x, int y) const;
  RECT LinkRect(int index) const;
  RECT ChromeRect(bool hide) const;

  double DisplayPct(double util) const;
  std::wstring SummaryLabel() const;
  COLORREF SummaryColor() const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  Config config_;
  AllUsage usage_{};
  bool hasUsage_ = false;
  std::string statusLine_ = "Loading...";
  std::string error_;
  bool refreshing_ = false;
  bool expanded_ = false;
  int dpi_ = 96;
  int hover_ = -1;

  NOTIFYICONDATAW nid_{};
  bool trayAdded_ = false;
  HICON trayIcon_ = nullptr;
  UINT taskbarCreated_ = 0;
  UINT wakeMsg_ = 0;

  bool dragging_ = false;
  bool movedWhileDrag_ = false;
  POINT dragStart_{};
  POINT windowStart_{};

  static constexpr int kCompactW = 188;
  static constexpr int kCompactH = 44;
  static constexpr int kExpandedW = 308;
  static constexpr int kExpandedHMin = 200;

  static constexpr UINT kTimerRefresh = 1;
  static constexpr UINT kTimerCountdown = 2;
  static constexpr UINT kMsgRefreshDone = WM_APP + 1;
  static constexpr UINT kTrayMsg = WM_APP + 2;
};

}  // namespace cu
