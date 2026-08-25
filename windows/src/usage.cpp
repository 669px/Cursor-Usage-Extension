#include "usage.hpp"
#include "auth.hpp"
#include "util.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdio>
#include <regex>
#include <vector>

namespace cu {

double ClampPct(double v) {
  if (!(v == v))
    return 0;
  return (std::min)(100.0, (std::max)(0.0, v));
}

static double PctFromMessage(const std::string& msg) {
  static const std::regex re(R"(([\d.]+)\s*%)");
  std::smatch m;
  if (std::regex_search(msg, m, re))
    return ClampPct(std::stod(m[1].str()));
  return 0;
}

struct HttpResult {
  std::optional<std::string> body;
  DWORD status = 0;
  std::string error;
};

static HttpResult HttpGet(const std::wstring& host, INTERNET_PORT port, const std::wstring& path,
                          const std::vector<std::pair<std::wstring, std::wstring>>& headers,
                          const std::wstring& proxyUrl) {
  HttpResult out;
  HINTERNET session = WinHttpOpen(L"ai-usage-widget/1.2",
                                  proxyUrl.empty() ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY
                                                   : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
                                  proxyUrl.empty() ? WINHTTP_NO_PROXY_NAME : proxyUrl.c_str(),
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    out.error = "Network init failed";
    return out;
  }
  WinHttpSetTimeouts(session, 5000, 5000, 15000, 15000);

  HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
  if (!connect) {
    WinHttpCloseHandle(session);
    out.error = "Connect failed";
    return out;
  }

  DWORD flags = (port == INTERNET_DEFAULT_HTTPS_PORT) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    out.error = "Request failed";
    return out;
  }

  for (const auto& h : headers) {
    std::wstring line = h.first + L": " + h.second + L"\r\n";
    WinHttpAddRequestHeaders(request, line.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
  }

  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    out.error = "API unreachable";
    return out;
  }

  DWORD statusSize = sizeof(out.status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &out.status, &statusSize, WINHTTP_NO_HEADER_INDEX);

  std::string body;
  DWORD avail = 0;
  while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
    std::vector<char> buf(avail);
    DWORD read = 0;
    if (!WinHttpReadData(request, buf.data(), avail, &read) || read == 0)
      break;
    body.append(buf.data(), read);
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);

  if (out.status != 200) {
    out.error = "HTTP " + std::to_string(out.status);
    return out;
  }
  out.body = std::move(body);
  return out;
}

static ProviderUsage NormalizeCursor(const std::string& json, bool showBilling) {
  ProviderUsage u;
  u.id = "cursor";
  u.tier = PlanLabel(JsonString(json, "membershipType"));
  if (u.tier == "CURSOR" && JsonString(json, "membershipType").empty())
    u.tier = "CURSOR";
  u.isUnlimited = JsonBool(json, "isUnlimited").value_or(false);

  auto individual = JsonObject(json, "individualUsage");
  auto plan = JsonObject(individual, "plan");
  auto onDemand = JsonObject(individual, "onDemand");
  std::string end = JsonString(json, "billingCycleEnd");

  double autoU = JsonNumber(plan, "autoPercentUsed")
                     ? ClampPct(*JsonNumber(plan, "autoPercentUsed"))
                     : PctFromMessage(JsonString(json, "autoModelSelectedDisplayMessage"));
  double apiU = JsonNumber(plan, "apiPercentUsed")
                    ? ClampPct(*JsonNumber(plan, "apiPercentUsed"))
                    : PctFromMessage(JsonString(json, "namedModelSelectedDisplayMessage"));
  double totalU = JsonNumber(plan, "totalPercentUsed")
                      ? ClampPct(*JsonNumber(plan, "totalPercentUsed"))
                      : (std::max)(autoU, apiU);

  u.a = {autoU, end, false};
  u.b = {apiU, end, false};
  u.total = {totalU, end, false};

  if (showBilling) {
    std::string parts;
    if (JsonBool(plan, "enabled").value_or(false)) {
      auto breakdown = JsonObject(plan, "breakdown");
      if (auto inc = JsonNumber(breakdown, "included")) {
        parts = CentsLabel(*inc);
        if (auto bonus = JsonNumber(breakdown, "bonus"); bonus && *bonus != 0)
          parts += " + " + CentsLabel(*bonus) + " bonus";
      }
    }
    if (JsonBool(onDemand, "enabled").value_or(false)) {
      if (!parts.empty())
        parts += " · ";
      auto used = JsonNumber(onDemand, "used").value_or(0);
      auto lim = JsonNumber(onDemand, "limit");
      parts += "on-demand " + CentsLabel(used) + " / " + (lim ? CentsLabel(*lim) : "∞");
    }
    u.billing = parts;
  }
  u.ok = true;
  return u;
}

static ProviderUsage NormalizeClaude(const std::string& json, const std::string& tier,
                                     bool showBilling) {
  ProviderUsage u;
  u.id = "claude";
  u.tier = PlanLabel(tier.empty() ? "claude" : tier);
  auto five = JsonObject(json, "five_hour");
  auto seven = JsonObject(json, "seven_day");
  double a = ClampPct(JsonNumber(five, "utilization").value_or(0));
  double b = ClampPct(JsonNumber(seven, "utilization").value_or(0));
  u.a = {a, JsonString(five, "resets_at"), false};
  u.b = {b, JsonString(seven, "resets_at"), false};
  u.total = {(std::max)(a, b), u.b.resetsAt.empty() ? u.a.resetsAt : u.b.resetsAt, false};
  if (showBilling) {
    auto extra = JsonObject(json, "extra_usage");
    if (JsonBool(extra, "is_enabled").value_or(false)) {
      auto used = JsonNumber(extra, "used_credits");
      auto lim = JsonNumber(extra, "monthly_limit");
      if (used && lim)
        u.billing = "extra " + std::to_string(static_cast<int>(*used)) + " / " +
                    std::to_string(static_cast<int>(*lim));
      else
        u.billing = "extra " + std::to_string(static_cast<int>(
                                   ClampPct(JsonNumber(extra, "utilization").value_or(0)))) +
                    "%";
    }
  }
  u.ok = true;
  return u;
}

static ProviderUsage NormalizeCodex(const std::string& json, bool showBilling) {
  ProviderUsage u;
  u.id = "codex";
  u.tier = PlanLabel(JsonString(json, "plan_type").empty() ? "codex" : JsonString(json, "plan_type"));
  auto rate = JsonObject(json, "rate_limit");
  auto primary = JsonObject(rate, "primary_window");
  auto secondary = JsonObject(rate, "secondary_window");

  auto fromWin = [](const std::string& win) -> Pool {
    if (win.empty() || win == "null")
      return {0, {}, true};
    Pool p;
    p.utilization = ClampPct(JsonNumber(win, "used_percent").value_or(0));
    if (auto reset = JsonNumber(win, "reset_at"))
      p.resetsAt = "unix:" + std::to_string(static_cast<long long>(*reset));
    return p;
  };

  u.a = fromWin(primary);
  if (u.a.missing)
    u.a = {0, {}, false};
  u.b = fromWin(secondary);
  u.total = {(std::max)(u.a.utilization, u.b.missing ? 0.0 : u.b.utilization),
             u.b.missing ? u.a.resetsAt : u.b.resetsAt, false};

  auto credits = JsonObject(json, "credits");
  u.isUnlimited = JsonBool(credits, "unlimited").value_or(false);
  if (showBilling && JsonBool(credits, "has_credits").value_or(false)) {
    if (u.isUnlimited)
      u.billing = "credits unlimited";
    else if (auto bal = JsonNumber(credits, "balance")) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "credits $%.2f", *bal);
      u.billing = buf;
    }
  }
  u.ok = true;
  return u;
}

static ProviderUsage FetchCursor(const Config& config) {
  ProviderUsage u;
  u.id = "cursor";
  auto token = ResolveCursorToken();
  if (!token) {
    u.error = "Login required";
    return u;
  }
  auto cookie = SessionCookieFromToken(*token);
  if (!cookie) {
    u.error = "Bad token";
    return u;
  }
  auto http = HttpGet(L"cursor.com", INTERNET_DEFAULT_HTTPS_PORT, L"/api/usage-summary",
                      {{L"Cookie", L"WorkosCursorSessionToken=" + Utf8ToWide(*cookie)},
                       {L"User-Agent", L"ai-usage-widget"}},
                      config.proxyUrl);
  if (!http.body) {
    u.error = http.error.empty() ? "API failed" : http.error;
    return u;
  }
  return NormalizeCursor(*http.body, config.showBilling);
}

static ProviderUsage FetchClaude(const Config& config) {
  ProviderUsage u;
  u.id = "claude";
  auto cred = ResolveClaudeCred();
  if (!cred) {
    u.error = "Login required";
    return u;
  }
  auto http = HttpGet(L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, L"/api/oauth/usage",
                      {{L"Authorization", L"Bearer " + Utf8ToWide(cred->accessToken)},
                       {L"anthropic-beta", L"oauth-2025-04-20"},
                       {L"User-Agent", L"claude-code/2.1.72"},
                       {L"Content-Type", L"application/json"}},
                      config.proxyUrl);
  if (!http.body) {
    u.error = http.error.empty() ? "API failed" : http.error;
    return u;
  }
  return NormalizeClaude(*http.body, cred->subscriptionType, config.showBilling);
}

static ProviderUsage FetchCodex(const Config& config) {
  ProviderUsage u;
  u.id = "codex";
  auto cred = ResolveCodexCred();
  if (!cred) {
    u.error = "Login required";
    return u;
  }
  std::vector<std::pair<std::wstring, std::wstring>> headers = {
      {L"Authorization", L"Bearer " + Utf8ToWide(cred->accessToken)},
      {L"Accept", L"application/json"},
      {L"User-Agent", L"ai-usage-widget"},
  };
  if (!cred->accountId.empty())
    headers.emplace_back(L"ChatGPT-Account-Id", Utf8ToWide(cred->accountId));

  auto http = HttpGet(L"chatgpt.com", INTERNET_DEFAULT_HTTPS_PORT, L"/backend-api/wham/usage",
                      headers, config.proxyUrl);
  if (!http.body) {
    u.error = http.error.empty() ? "API failed" : http.error;
    return u;
  }
  return NormalizeCodex(*http.body, config.showBilling);
}

AllUsage FetchAllUsage(const Config& config) {
  AllUsage all;
  if (config.showCursor)
    all.cursor = FetchCursor(config);
  if (config.showClaude)
    all.claude = FetchClaude(config);
  if (config.showCodex)
    all.codex = FetchCodex(config);
  return all;
}

const Pool* SelectPool(const ProviderUsage& data, const std::string& panelWindow) {
  if (panelWindow == "api")
    return data.b.missing ? &data.a : &data.b;
  if (panelWindow == "total")
    return &data.total;
  if (panelWindow == "auto")
    return &data.a;
  return data.a.utilization >= (data.b.missing ? -1.0 : data.b.utilization) ? &data.a : &data.b;
}

const ProviderUsage* SelectProvider(const AllUsage& all, const Config& config) {
  auto usable = [](const ProviderUsage& p) { return p.ok; };
  if (config.panelProvider == "cursor" && usable(all.cursor))
    return &all.cursor;
  if (config.panelProvider == "claude" && usable(all.claude))
    return &all.claude;
  if (config.panelProvider == "codex" && usable(all.codex))
    return &all.codex;

  const ProviderUsage* best = nullptr;
  double bestU = -1;
  for (const ProviderUsage* p : {&all.cursor, &all.claude, &all.codex}) {
    if (!usable(*p))
      continue;
    const Pool* pool = SelectPool(*p, config.panelWindow);
    double u = pool ? pool->utilization : 0;
    if (u > bestU) {
      bestU = u;
      best = p;
    }
  }
  return best;
}

}  // namespace cu
