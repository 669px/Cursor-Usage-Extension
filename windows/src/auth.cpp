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

static std::optional<std::string> TokenFromEnv(const wchar_t* name) {
  wchar_t buf[4096];
  DWORD n = GetEnvironmentVariableW(name, buf, 4096);
  if (n == 0 || n >= 4096)
    return std::nullopt;
  std::wstring w(buf);
  while (!w.empty() && (w.back() == L' ' || w.back() == L'\t'))
    w.pop_back();
  if (w.empty())
    return std::nullopt;
  return WideToUtf8(w);
}

static std::optional<std::string> TokenFromSqlite(const std::wstring& dbPath) {
  if (!FileExists(dbPath))
    return std::nullopt;

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
  BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &si, &pi);
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

std::optional<std::string> ResolveCursorToken() {
  if (auto env = TokenFromEnv(L"CURSOR_SESSION_TOKEN")) {
    std::string e = *env;
    auto cut = e.find("::");
    if (cut == std::string::npos)
      cut = e.find("%3A%3A");
    if (cut != std::string::npos) {
      if (e.compare(cut, 2, "::") == 0)
        return e.substr(cut + 2);
      return e.substr(cut + 6);
    }
    return e;
  }

  const std::wstring candidates[] = {
      HomeDir() + L"\\.cursor\\auth.json",
      AppDataDir() + L"\\cursor\\auth.json",
      LocalAppDataDir() + L"\\cursor\\auth.json",
  };
  for (const auto& path : candidates) {
    if (auto t = TokenFromAuthJson(path))
      return t;
  }

  return TokenFromSqlite(AppDataDir() + L"\\Cursor\\User\\globalStorage\\state.vscdb");
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

std::optional<ClaudeCred> ResolveClaudeCred() {
  if (auto env = TokenFromEnv(L"CLAUDE_CODE_OAUTH_TOKEN"))
    return ClaudeCred{*env, "CLAUDE"};

  auto raw = ReadFileUtf8(HomeDir() + L"\\.claude\\.credentials.json");
  if (!raw)
    return std::nullopt;
  auto oauth = JsonObject(*raw, "claudeAiOauth");
  if (oauth.empty())
    return std::nullopt;
  auto token = JsonString(oauth, "accessToken");
  if (token.empty())
    return std::nullopt;
  return ClaudeCred{token, JsonString(oauth, "subscriptionType")};
}

std::optional<CodexCred> ResolveCodexCred() {
  auto raw = ReadFileUtf8(HomeDir() + L"\\.codex\\auth.json");
  if (!raw)
    return std::nullopt;
  auto tokens = JsonObject(*raw, "tokens");
  auto token = JsonString(tokens, "access_token");
  if (token.empty())
    token = JsonString(*raw, "access_token");
  if (token.empty())
    return std::nullopt;
  auto account = JsonString(tokens, "account_id");
  if (account.empty())
    account = JsonString(*raw, "account_id");
  return CodexCred{token, account};
}

}  // namespace cu
