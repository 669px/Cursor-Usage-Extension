#pragma once

#include <optional>
#include <string>

namespace cu {

// Resolves a Cursor access token (JWT), not the full session cookie.
std::optional<std::string> ResolveAccessToken();

// Builds WorkosCursorSessionToken cookie value from a JWT access token.
std::optional<std::string> SessionCookieFromToken(const std::string& accessToken);

}  // namespace cu
