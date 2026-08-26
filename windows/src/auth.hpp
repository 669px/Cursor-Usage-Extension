#pragma once

#include <optional>
#include <string>

namespace cu {

// Resolves Cursor JWT access token.
std::optional<std::string> ResolveCursorToken();
std::optional<std::string> SessionCookieFromToken(const std::string& accessToken);

struct ClaudeCred {
  std::string accessToken;
  std::string subscriptionType;
  bool refreshed = false;  // true when this call rotated the stored token
};

// Finds the credentials the Claude Code CLI already wrote — env var, any of the
// .credentials.json locations, or the Windows Credential Manager — and silently
// renews them through the OAuth refresh token when they have expired. The user
// never has to log in again separately for the widget.
//
// `reason` receives a short, displayable explanation when this returns nullopt.
// `force` refreshes even when the stored token still looks valid, for the case
// where the server rejected it early (revoked token, or a skewed local clock).
std::optional<ClaudeCred> ResolveClaudeCred(const std::wstring& proxyUrl, std::string* reason,
                                            bool force = false);

struct CodexCred {
  std::string accessToken;
  std::string accountId;
};
std::optional<CodexCred> ResolveCodexCred();

}  // namespace cu
