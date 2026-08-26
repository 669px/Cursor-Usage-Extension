#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cu {

using Headers = std::vector<std::pair<std::wstring, std::wstring>>;

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;  // empty when the transport succeeded

  bool ok() const { return error.empty() && status == 200; }
};

// Performs a request over HTTPS. `body` is sent only when non-empty.
HttpResponse HttpRequest(const std::wstring& method, const std::wstring& host,
                         const std::wstring& path, const Headers& headers,
                         const std::string& body, const std::wstring& proxyUrl);

HttpResponse HttpGet(const std::wstring& host, const std::wstring& path, const Headers& headers,
                     const std::wstring& proxyUrl);

// Turns a status code into something worth showing in 180px of widget.
std::string HttpStatusLabel(long status);

}  // namespace cu
