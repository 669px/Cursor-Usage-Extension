#pragma once

#include <string>

namespace cu {

struct Config {
  int refreshIntervalSec = 300;
  std::string panelWindow = "max";   // max | auto | api | total
  std::string usageDisplay = "used"; // used | remaining
  bool showBilling = false;
  bool showTier = true;
  bool alwaysOnTop = true;
  bool startCompact = true;
  bool startHidden = false;
  int opacity = 245;  // 80–255
  int windowX = -1;
  int windowY = -1;
  std::wstring proxyUrl;

  void Load();
  void Save() const;

 private:
  static std::wstring ConfigPath();
};

}  // namespace cu
