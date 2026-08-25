#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace cu {

std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

std::wstring ExpandEnv(const std::wstring& path);
std::wstring HomeDir();
std::wstring AppDataDir();
std::wstring LocalAppDataDir();

std::optional<std::string> ReadFileUtf8(const std::wstring& path);
bool FileExists(const std::wstring& path);

std::string Base64UrlDecode(const std::string& in);
std::optional<std::string> JwtPayloadJson(const std::string& token);

std::string JsonString(const std::string& json, const std::string& key);
std::optional<double> JsonNumber(const std::string& json, const std::string& key);
std::optional<bool> JsonBool(const std::string& json, const std::string& key);
std::string JsonObject(const std::string& json, const std::string& key);

std::string HumanDuration(int64_t seconds);
std::string RelativeReset(const std::string& iso);
std::optional<int64_t> SecondsUntilIso(const std::string& iso);
std::string PlanLabel(const std::string& value);
std::string CentsLabel(double cents);
std::string FormatTimeNow();

void OpenUrl(const std::wstring& url);

}  // namespace cu
