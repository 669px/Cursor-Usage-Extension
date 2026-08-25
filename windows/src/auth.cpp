#include "auth.hpp"
#include "util.hpp"

#include <windows.h>

#include <string>

namespace cu {

static std::optional<std::string> TokenFromAuthJson(const std::wstring& path) {
  auto raw = ReadFileUtf8(path);
  if (!raw)
    return std::nullopt;
  auto token = JsonString(*raw, "accessToken");
  if (token.empty())
    token = JsonString(*raw, "access_token");
  if (token.empty())
    return std::nullopt;
  return token;
}

static std::optional<std::string> TokenFromEnv() {
  wchar_t buf[4096];
  DWORD n = GetEnvironmentVariableW(L"CURSOR_SESSION_TOKEN", buf, 4096);
  if (n == 0 || n >= 4096)
    return std::nullopt;
  std::wstring w(buf);
  while (!w.empty() && (w.back() == L' ' || w.back() == L'\t'))
    w.pop_back();
  if (w.empty())
    return std::nullopt;

  std::string env = WideToUtf8(w);
  auto cut = env.find("::");
  if (cut == std::string::npos)
    cut = env.find("%3A%3A");
  if (cut != std::string::npos) {
    if (env.compare(cut, 2, "::") == 0)
      return env.substr(cut + 2);
    return env.substr(cut + 6);
  }
  return env;
}

static std::optional<std::string> TokenFromSqlite(const std::wstring& dbPath) {
  if (!FileExists(dbPath))
    return std::nullopt;

  // Prefer sqlite3 on PATH (same approach as the GNOME extension).
  std::wstring cmd = L"sqlite3 \"" + dbPath +
                     L"\" \"SELECT value FROM ItemTable WHERE key='cursorAuth/accessToken' LIMIT 1;\"";

  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE readPipe = nullptr, writePipe = nullptr;
  if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    return std::nullopt;
  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = writePipe;
  si.hStdError = writePipe;
  si.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION pi{};
  std::wstring mutableCmd = cmd;
  BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(writePipe);
  if (!ok) {
    CloseHandle(readPipe);
    return std::nullopt;
  }

  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0)
    out.append(buf, buf + n);

  WaitForSingleObject(pi.hProcess, 5000);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseHandle(readPipe);

  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
    out.pop_back();
  if (out.empty())
    return std::nullopt;
  return out;
}

std::optional<std::string> ResolveAccessToken() {
  if (auto env = TokenFromEnv())
    return env;

  const std::wstring candidates[] = {
      HomeDir() + L"\\.cursor\\auth.json",
      AppDataDir() + L"\\cursor\\auth.json",
      LocalAppDataDir() + L"\\cursor\\auth.json",
  };
  for (const auto& path : candidates) {
    if (auto t = TokenFromAuthJson(path))
      return t;
  }

  const std::wstring db = AppDataDir() + L"\\Cursor\\User\\globalStorage\\state.vscdb";
  return TokenFromSqlite(db);
}

std::optional<std::string> SessionCookieFromToken(const std::string& accessToken) {
  auto payload = JwtPayloadJson(accessToken);
  if (!payload)
    return std::nullopt;
  auto sub = JsonString(*payload, "sub");
  if (sub.empty())
    return std::nullopt;
  auto pipe = sub.rfind('|');
  if (pipe != std::string::npos)
    sub = sub.substr(pipe + 1);
  return sub + "%3A%3A" + accessToken;
}

}  // namespace cu
