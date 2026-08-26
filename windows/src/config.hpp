#pragma once

#include <limits>
#include <string>

namespace cu {

struct Config {
  // A saved position of -1 used to mean "unset", which made it impossible to
  // park the widget on a monitor left of / above the primary one.
  static constexpr int kNoPosition = (std::numeric_limits<int>::min)();

  int refreshIntervalSec = 300;
  std::string panelProvider = "max";  // max | cursor | claude | codex
  std::string panelWindow = "max";    // max | auto | api | total
  std::string usageDisplay = "used";  // used | remaining
  bool showBilling = false;
  bool showTier = true;
  bool showCursor = true;
  bool showClaude = true;
  bool showCodex = true;
  bool alwaysOnTop = true;
  bool startCompact = true;
  bool startHidden = false;
  int opacity = 245;
  int windowX = kNoPosition;
  int windowY = kNoPosition;
  std::wstring proxyUrl;

  void Load();
  void Save() const;

 private:
  static std::wstring ConfigPath();
};

}  // namespace cu
