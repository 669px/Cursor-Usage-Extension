#include "usage.hpp"
#include "auth.hpp"
#include "util.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
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

static UsageData Normalize(const std::string& json) {
  UsageData u;
  u.tier = PlanLabel(JsonString(json, "membershipType"));
  u.isUnlimited = JsonBool(json, "isUnlimited").value_or(false);

  auto individual = JsonObject(json, "individualUsage");
  auto plan = JsonObject(individual, "plan");
  auto onDemand = JsonObject(individual, "onDemand");

  std::string end = JsonString(json, "billingCycleEnd");
  auto autoPct = JsonNumber(plan, "autoPercentUsed");
  auto apiPct = JsonNumber(plan, "apiPercentUsed");
  auto totalPct = JsonNumber(plan, "totalPercentUsed");

  double autoU = autoPct ? ClampPct(*autoPct)
                         : PctFromMessage(JsonString(json, "autoModelSelectedDisplayMessage"));
  double apiU = apiPct ? ClampPct(*apiPct)
                       : PctFromMessage(JsonString(json, "namedModelSelectedDisplayMessage"));
  double totalU = totalPct ? ClampPct(*totalPct) : (std::max)(autoU, apiU);

  u.autoPool = {autoU, end};
  u.apiPool = {apiU, end};
  u.totalPool = {totalU, end};

  if (JsonBool(plan, "enabled").value_or(false)) {
    auto breakdown = JsonObject(plan, "breakdown");
    u.planIncludedCents = JsonNumber(breakdown, "included");
    u.planBonusCents = JsonNumber(breakdown, "bonus");
  }

  if (JsonBool(onDemand, "enabled").value_or(false)) {
    u.onDemandEnabled = true;
    u.onDemandUsed = JsonNumber(onDemand, "used").value_or(0);
    u.onDemandLimit = JsonNumber(onDemand, "limit");
  }
  return u;
}

struct HttpResult {
  std::optional<std::string> body;
  DWORD status = 0;
  std::string error;
};

static HttpResult HttpGet(const std::wstring& host, INTERNET_PORT port, const std::wstring& path,
                          const std::string& cookie, const std::wstring& proxyUrl) {
  HttpResult out;
  HINTERNET session = WinHttpOpen(L"cursor-usage-widget/1.1",
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

  HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE);
  if (!request) {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    out.error = "Request failed";
    return out;
  }

  std::wstring headers = L"Cookie: WorkosCursorSessionToken=" + Utf8ToWide(cookie) +
                         L"\r\nUser-Agent: cursor-usage-widget\r\n";
  WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1L),
                           WINHTTP_ADDREQ_FLAG_ADD);

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

FetchResult FetchUsage(const Config& config) {
  FetchResult r;
  auto token = ResolveAccessToken();
  if (!token) {
    r.error = "Login required";
    return r;
  }
  auto cookie = SessionCookieFromToken(*token);
  if (!cookie) {
    r.error = "Bad token";
    return r;
  }

  auto http = HttpGet(L"cursor.com", INTERNET_DEFAULT_HTTPS_PORT, L"/api/usage-summary", *cookie,
                      config.proxyUrl);
  if (!http.body) {
    r.error = http.error.empty() ? "API failed" : http.error;
    return r;
  }

  r.ok = true;
  r.data = Normalize(*http.body);
  return r;
}

const Pool* SelectPool(const UsageData& data, const std::string& panelWindow) {
  if (panelWindow == "api")
    return &data.apiPool;
  if (panelWindow == "total")
    return &data.totalPool;
  if (panelWindow == "auto")
    return &data.autoPool;
  return data.apiPool.utilization > data.autoPool.utilization ? &data.apiPool : &data.autoPool;
}

}  // namespace cu
