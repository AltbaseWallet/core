#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>

// Zano exposes a zero-based wallet top index, but daemon height is a count.
inline uint64_t zano_scanned_block_count(uint64_t top_index) {
  return top_index == std::numeric_limits<uint64_t>::max() ? top_index : top_index + 1;
}

// A running wallet or a positive cached balance does not prove the scan is current.
inline bool zano_native_scan_ready(std::string_view state,
                                  std::string_view wallet_text,
                                  std::string_view daemon_text) {
  uint64_t wallet = 0, daemon = 0;
  const auto parse = [](std::string_view text, uint64_t& value) {
    if (text.empty()) return false;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
  };
  return state == "2" && parse(wallet_text, wallet) && parse(daemon_text, daemon)
    && daemon > 0 && zano_scanned_block_count(wallet) >= daemon;
}
