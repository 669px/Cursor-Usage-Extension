#pragma once

#include "config.hpp"

#include <optional>
#include <string>

namespace cu {

struct Pool {
  double utilization = 0;
  std::string resetsAt;  // ISO or empty; unix seconds stored as decimal string with prefix 'u:'
  bool missing = false;
};

struct ProviderUsage {
  std::string id;
  std::string tier = "AI";
  bool isUnlimited = false;
  bool ok = false;
  std::string error;
  Pool a;
  Pool b;
  Pool total;
  std::string billing;
};

struct AllUsage {
  ProviderUsage cursor;
  ProviderUsage claude;
  ProviderUsage codex;
};

AllUsage FetchAllUsage(const Config& config);

double ClampPct(double v);
const Pool* SelectPool(const ProviderUsage& data, const std::string& panelWindow);
const ProviderUsage* SelectProvider(const AllUsage& all, const Config& config);

}  // namespace cu
