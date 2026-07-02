#include "protocol.hpp"
#include "wallet_core.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef ALTBASE_CORE_VERSION
#define ALTBASE_CORE_VERSION "0.1.4"
#endif

namespace {
constexpr const char* kBridgeLaunchArg = "--altbase-wallet-bridge";

bool env_bridge_launch_enabled() {
#ifdef _WIN32
  char* value = nullptr;
  size_t length = 0;
  if (::_dupenv_s(&value, &length, "ALTBASE_CORE_BRIDGE") != 0 || value == nullptr) return false;
  const std::string text(value, length > 0 ? length - 1 : 0);
  std::free(value);
  return text == "1";
#else
  const char* env = std::getenv("ALTBASE_CORE_BRIDGE");
  return env != nullptr && std::string(env) == "1";
#endif
}

bool is_bridge_launch(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == kBridgeLaunchArg) return true;
  }
  return env_bridge_launch_enabled();
}

void show_manual_launch_message() {
  const std::string message =
    std::string("Altbase Core Bridge v") + ALTBASE_CORE_VERSION +
    "\n\nThis helper component is started automatically by Altbase Wallet.";
  std::cout << message << std::endl;

#ifdef _WIN32
  MessageBoxA(
    nullptr,
    message.c_str(),
    "Altbase Core Bridge",
    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND
  );
#endif
}
}

int main(int argc, char** argv) {
  if (!is_bridge_launch(argc, argv)) {
    show_manual_launch_message();
    return 0;
  }

  altbase::WalletCore core;
  std::string line;

  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;

    std::string parse_error;
    auto request = altbase::parse_request(line, parse_error);
    if (!request) {
      std::cout << altbase::error_response("", "bad_request", parse_error) << std::endl;
      continue;
    }

    try {
      request->params["requestId"] = request->id;
      const auto result = core.handle(*request);
      std::cout << altbase::ok_response(request->id, result) << std::endl;
    } catch (const std::exception& error) {
      std::cout << altbase::error_response(request->id, "core_error", error.what()) << std::endl;
    }
  }

  return 0;
}
