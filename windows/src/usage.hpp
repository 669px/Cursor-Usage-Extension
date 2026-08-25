#pragma once

#include "config.hpp"

#include <optional>
#include <string>

namespace cu {

struct Pool {
  double utilization = 0;
  std::string resetsAt;
};

struct UsageData {
  std::string tier = "CURSOR";
  bool isUnlimited = false;
  Pool autoPool;
  Pool apiPool;
  Pool totalPool;
  std::optional<double> planIncludedCents;
  std::optional<double> planBonusCents;
  bool onDemandEnabled = false;
  double onDemandUsed = 0;
  std::optional<double> onDemandLimit;
};

struct FetchResult {
  bool ok = false;
  std::string error;
  UsageData data;
};

FetchResult FetchUsage(const Config& config);

double ClampPct(double v);
const Pool* SelectPool(const UsageData& data, const std::string& panelWindow);

}  // namespace cu
