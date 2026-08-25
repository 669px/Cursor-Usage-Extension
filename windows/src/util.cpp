#include "util.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace cu {

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string WideToUtf8(const std::wstring& s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring ExpandEnv(const std::wstring& path) {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = ExpandEnvironmentStringsW(path.c_str(), buf, MAX_PATH * 4);
  if (n == 0 || n > MAX_PATH * 4)
    return path;
  return buf;
}

std::wstring HomeDir() {
  return ExpandEnv(L"%USERPROFILE%");
}

std::wstring AppDataDir() {
  return ExpandEnv(L"%APPDATA%");
}

std::wstring LocalAppDataDir() {
  return ExpandEnv(L"%LOCALAPPDATA%");
}

bool FileExists(const std::wstring& path) {
  DWORD attr = GetFileAttributesW(path.c_str());
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

std::optional<std::string> ReadFileUtf8(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in)
    return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static int B64Val(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

std::string Base64UrlDecode(const std::string& in) {
  std::string s = in;
  for (char& c : s) {
    if (c == '-')
      c = '+';
    else if (c == '_')
      c = '/';
  }
  while (s.size() % 4)
    s.push_back('=');

  std::string out;
  out.reserve(s.size() * 3 / 4);
  for (size_t i = 0; i + 3 < s.size(); i += 4) {
    int a = B64Val(s[i]);
    int b = B64Val(s[i + 1]);
    int c = B64Val(s[i + 2]);
    int d = B64Val(s[i + 3]);
    if (a < 0 || b < 0)
      break;
    out.push_back(static_cast<char>((a << 2) | (b >> 4)));
    if (s[i + 2] != '=' && c >= 0)
      out.push_back(static_cast<char>(((b & 15) << 4) | (c >> 2)));
    if (s[i + 3] != '=' && d >= 0)
      out.push_back(static_cast<char>(((c & 3) << 6) | d));
  }
  return out;
}

std::optional<std::string> JwtPayloadJson(const std::string& token) {
  auto p1 = token.find('.');
  if (p1 == std::string::npos)
    return std::nullopt;
  auto p2 = token.find('.', p1 + 1);
  if (p2 == std::string::npos)
    return std::nullopt;
  try {
    return Base64UrlDecode(token.substr(p1 + 1, p2 - p1 - 1));
  } catch (...) {
    return std::nullopt;
  }
}

static size_t SkipWs(const std::string& j, size_t i) {
  while (i < j.size() && (j[i] == ' ' || j[i] == '\t' || j[i] == '\n' || j[i] == '\r'))
    ++i;
  return i;
}

static size_t FindKey(const std::string& j, const std::string& key, size_t from = 0) {
  const std::string pat = "\"" + key + "\"";
  size_t i = from;
  while (true) {
    i = j.find(pat, i);
    if (i == std::string::npos)
      return std::string::npos;
    size_t k = SkipWs(j, i + pat.size());
    if (k < j.size() && j[k] == ':')
      return k + 1;
    i += pat.size();
  }
}

static size_t SkipValue(const std::string& j, size_t i) {
  i = SkipWs(j, i);
  if (i >= j.size())
    return i;
  if (j[i] == '"') {
    ++i;
    while (i < j.size()) {
      if (j[i] == '\\') {
        i += 2;
        continue;
      }
      if (j[i] == '"')
        return i + 1;
      ++i;
    }
    return i;
  }
  if (j[i] == '{' || j[i] == '[') {
    char open = j[i];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool inStr = false;
    for (; i < j.size(); ++i) {
      char c = j[i];
      if (inStr) {
        if (c == '\\') {
          ++i;
          continue;
        }
        if (c == '"')
          inStr = false;
        continue;
      }
      if (c == '"') {
        inStr = true;
        continue;
      }
      if (c == open)
        ++depth;
      else if (c == close) {
        --depth;
        if (depth == 0)
          return i + 1;
      }
    }
    return i;
  }
  while (i < j.size() && j[i] != ',' && j[i] != '}' && j[i] != ']' && !std::isspace(static_cast<unsigned char>(j[i])))
    ++i;
  return i;
}

std::string JsonObject(const std::string& json, const std::string& key) {
  size_t v = FindKey(json, key);
  if (v == std::string::npos)
    return {};
  v = SkipWs(json, v);
  if (v >= json.size() || json[v] != '{')
    return {};
  size_t end = SkipValue(json, v);
  return json.substr(v, end - v);
}

std::string JsonString(const std::string& json, const std::string& key) {
  size_t v = FindKey(json, key);
  if (v == std::string::npos)
    return {};
  v = SkipWs(json, v);
  if (v >= json.size() || json[v] != '"')
    return {};
  ++v;
  std::string out;
  while (v < json.size()) {
    char c = json[v++];
    if (c == '\\' && v < json.size()) {
      char e = json[v++];
      if (e == 'n')
        out.push_back('\n');
      else if (e == 't')
        out.push_back('\t');
      else if (e == 'r')
        out.push_back('\r');
      else
        out.push_back(e);
      continue;
    }
    if (c == '"')
      break;
    out.push_back(c);
  }
  return out;
}

std::optional<double> JsonNumber(const std::string& json, const std::string& key) {
  size_t v = FindKey(json, key);
  if (v == std::string::npos)
    return std::nullopt;
  v = SkipWs(json, v);
  if (v >= json.size())
    return std::nullopt;
  if (json.compare(v, 4, "null") == 0)
    return std::nullopt;
  try {
    size_t consumed = 0;
    double n = std::stod(json.substr(v), &consumed);
    if (consumed == 0)
      return std::nullopt;
    return n;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<bool> JsonBool(const std::string& json, const std::string& key) {
  size_t v = FindKey(json, key);
  if (v == std::string::npos)
    return std::nullopt;
  v = SkipWs(json, v);
  if (json.compare(v, 4, "true") == 0)
    return true;
  if (json.compare(v, 5, "false") == 0)
    return false;
  return std::nullopt;
}

std::string HumanDuration(int64_t seconds) {
  int64_t s = (std::max)(int64_t{0}, seconds);
  if (s < 60)
    return std::to_string(s) + "s";
  int64_t mins = (s + 30) / 60;
  if (mins < 60)
    return std::to_string(mins) + "m";
  int64_t hrs = mins / 60;
  if (hrs < 24)
    return std::to_string(hrs) + "h " + std::to_string(mins % 60) + "m";
  int64_t days = hrs / 24;
  return std::to_string(days) + "d " + std::to_string(hrs % 24) + "h";
}

static std::optional<std::chrono::system_clock::time_point> ParseIso(const std::string& iso) {
  if (iso.size() < 19)
    return std::nullopt;
  std::tm tm{};
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 6)
    return std::nullopt;
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = se;
#if defined(_WIN32)
  time_t t = _mkgmtime(&tm);
#else
  time_t t = timegm(&tm);
#endif
  if (t < 0)
    return std::nullopt;
  return std::chrono::system_clock::from_time_t(t);
}

std::string RelativeReset(const std::string& iso) {
  if (iso.rfind("unix:", 0) == 0) {
    try {
      int64_t sec = std::stoll(iso.substr(5));
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
      auto diff = sec - now;
      if (diff <= 0)
        return "resetting";
      return "resets in " + HumanDuration(diff);
    } catch (...) {
      return {};
    }
  }
  auto diff = SecondsUntilIso(iso);
  if (!diff)
    return {};
  if (*diff <= 0)
    return "resetting";
  return "resets in " + HumanDuration(*diff);
}

std::optional<int64_t> SecondsUntilIso(const std::string& iso) {
  auto tp = ParseIso(iso);
  if (!tp)
    return std::nullopt;
  return std::chrono::duration_cast<std::chrono::seconds>(*tp - std::chrono::system_clock::now())
      .count();
}

std::string PlanLabel(const std::string& value) {
  std::string raw = value;
  while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front())))
    raw.erase(raw.begin());
  while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back())))
    raw.pop_back();
  if (raw.empty())
    return "CURSOR";

  std::string n;
  n.reserve(raw.size());
  for (char c : raw) {
    if (c == ' ' || c == '_' || c == '-')
      continue;
    n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  struct Pair { const char* key; const char* label; };
  static const Pair known[] = {
      {"ultra", "ULTRA"}, {"proplus", "PRO+"}, {"business", "BUSINESS"},
      {"enterprise", "ENT"}, {"pro", "PRO"}, {"max", "MAX"}, {"free", "FREE"},
      {"team", "TEAM"}, {"student", "STUDENT"}, {"go", "GO"}, {"plus", "PLUS"},
  };
  for (const auto& k : known) {
    if (n.find(k.key) != std::string::npos)
      return k.label;
  }
  for (char& c : raw)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return raw;
}

std::string CentsLabel(double cents) {
  if (!std::isfinite(cents))
    return "-";
  char buf[64];
  std::snprintf(buf, sizeof(buf), "$%.2f", cents / 100.0);
  return buf;
}

std::string FormatTimeNow() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02u:%02u", st.wHour, st.wMinute);
  return buf;
}

void OpenUrl(const std::wstring& url) {
  ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace cu
