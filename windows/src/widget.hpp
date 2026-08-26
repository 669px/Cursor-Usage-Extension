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
#include <vector>

namespace cu {

// One horizontal bar in the expanded card.
struct MeterRow {
  std::wstring name;
  std::wstring value;
  std::wstring caption;  // "resets in 3h 20m"
  double util = 0;
  bool filled = false;  // false for unknown / unlimited pools
  bool muted = false;
};

// One provider block. Built once per render so measuring and drawing can never
// disagree about how tall the card should be.
struct Section {
  std::wstring title;
  std::wstring tier;
  COLORREF accent = 0;
  std::vector<MeterRow> rows;
  std::wstring note;
  bool noteIsError = false;
};

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
    Close,
    Body,
  };

  // Every interactive rectangle, resolved once per layout pass and reused by
  // both the renderer and hit testing.
  struct Layout {
    RECT card{};
    RECT collapse{};
    RECT close{};
    RECT buttons[3]{};
    int cardW = 0;
    int cardH = 0;
  };

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

  int S(int v) const;
  int Shadow() const;

  std::vector<Section> BuildSections() const;
  int MeasureSections(const std::vector<Section>& sections) const;
  Layout ComputeLayout() const;

  void Relayout();
  void ClampToWorkArea(int& x, int& y, int w, int h) const;
  void Render();
  void DrawCard(void* graphics, const Layout& layout);
  void DrawRing(void* graphics, float cx, float cy, float radius, float stroke, double util,
                bool known) const;

  void RefreshAsync();
  void ApplyUsage(const AllUsage& result);
  void ScheduleCountdown();
  void UpdateTray();
  void CreateTray();
  void DestroyTray();
  HICON MakeTrayIcon(int size);
  void ShowContextMenu();
  void OpenSettings();
  void OpenDashboard();
  void PersistPosition();
  void SetExpanded(bool expanded);
  void SnapToEdges(int& x, int& y) const;

  Hit HitTest(int x, int y) const;

  double DisplayPct(double util) const;
  std::wstring SummaryLabel() const;
  COLORREF SummaryColor() const;
  std::wstring SummaryProvider() const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  Config config_;
  AllUsage usage_{};
  bool hasUsage_ = false;
  std::string statusLine_ = "Loading";
  std::string error_;
  bool refreshing_ = false;
  bool expanded_ = false;
  int dpi_ = 96;
  Hit hover_ = Hit::None;
  bool mouseInside_ = false;
  bool settingsOpen_ = false;

  NOTIFYICONDATAW nid_{};
  bool trayAdded_ = false;
  HICON trayIcon_ = nullptr;
  UINT taskbarCreated_ = 0;
  UINT wakeMsg_ = 0;

  bool dragging_ = false;
  bool movedWhileDrag_ = false;
  POINT dragStart_{};
  POINT windowStart_{};

  // Logical (96 dpi) metrics. The card is what you see; the window is the card
  // plus a transparent margin the drop shadow is painted into.
  static constexpr int kShadow = 16;
  static constexpr int kRadius = 14;
  static constexpr int kPadX = 16;
  static constexpr int kCompactW = 200;
  static constexpr int kCompactH = 48;
  static constexpr int kExpandedW = 324;

  static constexpr int kHeaderH = 78;
  static constexpr int kSectionTitleH = 22;
  static constexpr int kMeterLabelH = 18;
  static constexpr int kMeterBarH = 6;
  static constexpr int kMeterCaptionH = 15;
  static constexpr int kMeterGap = 9;
  static constexpr int kSectionGap = 12;
  static constexpr int kNoteH = 15;
  static constexpr int kFooterH = 48;

  static constexpr UINT kTimerRefresh = 1;
  static constexpr UINT kTimerCountdown = 2;
  static constexpr UINT kMsgRefreshDone = WM_APP + 1;
  static constexpr UINT kTrayMsg = WM_APP + 2;
};

}  // namespace cu
