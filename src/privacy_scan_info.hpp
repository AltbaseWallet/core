#pragma once

#include <exception>
#include <string>

namespace altbase {

// This read includes DNS and TLS establishment. The previous 10 s budget
// expired before TLS completed on a measured 16.3 s request from this host.
constexpr unsigned long PRIVACY_SCAN_INFO_TIMEOUT_MS = 45'000;

template <typename Get>
std::string read_privacy_scan_info(const std::string& base, const std::string& coin, Get get) {
  try {
    const auto response = get(base + "/" + coin + "/privacy/scan-info", PRIVACY_SCAN_INFO_TIMEOUT_MS);
    if (response.status < 200 || response.status >= 300) return "server scan-info HTTP " + std::to_string(response.status);
    return response.body;
  } catch (const std::exception& error) {
    return std::string("server scan-info unavailable: ") + error.what();
  }
}

} // namespace altbase
