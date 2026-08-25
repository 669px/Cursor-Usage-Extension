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
};
std::optional<ClaudeCred> ResolveClaudeCred();

struct CodexCred {
  std::string accessToken;
  std::string accountId;
};
std::optional<CodexCred> ResolveCodexCred();

}  // namespace cu
