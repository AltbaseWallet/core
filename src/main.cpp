#include "utxo_services_api.hpp"
#include "wallet_vault_api.hpp"
#include "coin_node_modules_api.hpp"
#ifdef ALTBASE_SEPARATE_PRIVACY_MODULES
#include "epic_wallet_api.hpp"
#include "monero_wallet_api.hpp"
#include "zano_wallet_api.hpp"
#endif
#include "protocol.hpp"
#include "utxo_wallet_modules_api.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#ifndef ALTBASE_CORE_VERSION
#define ALTBASE_CORE_VERSION "0.1.0"
#endif

namespace {
constexpr const char* kBridgeLaunchArg = "--altbase-wallet-bridge";

char* copy_response(const std::string& response) {
  auto* out = static_cast<char*>(std::malloc(response.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, response.c_str(), response.size() + 1);
  return out;
}

void bridge_free(char* value) {
  std::free(value);
}

#ifdef ALTBASE_SEPARATE_PRIVACY_MODULES
struct MoneroWalletModule {
  AltbaseMoneroWalletRequest request = nullptr;
  AltbaseMoneroWalletFree free = nullptr;
  std::string error;
#ifdef _WIN32
  HMODULE handle = nullptr;
#else
  void* handle = nullptr;
#endif
};

std::filesystem::path executable_directory() {
#ifdef _WIN32
  std::wstring buffer(32768, L'\0');
  const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 || size >= buffer.size()) return {};
  buffer.resize(size);
  return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
  buffer.resize(std::strlen(buffer.c_str()));
  return std::filesystem::weakly_canonical(buffer).parent_path();
#else
  std::string buffer(4096, '\0');
  const auto size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (size <= 0) return {};
  buffer.resize(static_cast<size_t>(size));
  return std::filesystem::path(buffer).parent_path();
#endif
}

MoneroWalletModule load_monero_wallet_module() {
  MoneroWalletModule module;
#ifdef _WIN32
  const auto path = executable_directory() / "altbase_monero_wallet.dll";
  module.handle = LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!module.handle) {
    module.error = "altbase_monero_wallet.dll could not be loaded (Windows error "
      + std::to_string(GetLastError()) + ")";
    return module;
  }
  module.request = reinterpret_cast<AltbaseMoneroWalletRequest>(
    GetProcAddress(module.handle, "altbase_monero_wallet_request"));
  module.free = reinterpret_cast<AltbaseMoneroWalletFree>(
    GetProcAddress(module.handle, "altbase_monero_wallet_free"));
#else
#ifdef __APPLE__
  constexpr const char* filename = "altbase_monero_wallet.dylib";
#else
  constexpr const char* filename = "altbase_monero_wallet.so";
#endif
  const auto path = executable_directory() / filename;
  module.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module.handle) {
    const char* detail = dlerror();
    module.error = std::string(filename) + " could not be loaded"
      + (detail ? std::string(": ") + detail : "");
    return module;
  }
  module.request = reinterpret_cast<AltbaseMoneroWalletRequest>(
    dlsym(module.handle, "altbase_monero_wallet_request"));
  module.free = reinterpret_cast<AltbaseMoneroWalletFree>(
    dlsym(module.handle, "altbase_monero_wallet_free"));
#endif
  if (!module.request || !module.free) {
    module.request = nullptr;
    module.free = nullptr;
    module.error = "Monero wallet module ABI exports are missing";
  }
  return module;
}

MoneroWalletModule& monero_wallet_module() {
  static MoneroWalletModule module = load_monero_wallet_module();
  return module;
}

char* dispatch_monero_wallet(
  const std::string& line,
  const std::string& request_id,
  void (**release)(char*)
) {
  auto& module = monero_wallet_module();
  if (!module.request || !module.free) {
    *release = bridge_free;
    return copy_response(altbase::error_response(
      request_id,
      "monero-module-load-error",
      module.error.empty() ? "Monero wallet module could not be loaded" : module.error));
  }
  *release = module.free;
  return module.request(line.c_str());
}
#endif

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

char* dispatch_request(const std::string& line, void (**release)(char*)) {
  std::string parse_error;
  const auto parsed = altbase::parse_request(line, parse_error);
  if (parsed.has_value() && parsed->method == "listWalletModules") {
    *release = bridge_free;
    return copy_response(altbase::ok_response(parsed->id, {
      {"utxo", "bitcoincash,digibyte,peercoin,bitcoin,bitcoin2,bitcoincashii,firo,btgs,capstash,hypercoin,mydogecoin,pepecoin,kerrigan,scash,litecoinii,neoxa,terracoin,junkcoin,raptoreum,pearl"},
      {"privacy", "zano,epic,monero"},
      {"account", "quai,xgr,qubic"},
      {"dag", "kaspa,nonsense"},
      {"cell", "ckb"},
      {"node", "nexa,zcash,bitcoincash,digibyte,peercoin,bitcoin,bitcoin2,bitcoincashii,firo,btgs,capstash,hypercoin,mydogecoin,pepecoin,kerrigan,scash,litecoinii,neoxa,terracoin,junkcoin,raptoreum,pearl,zano,epic,quai,xgr,qubic,kaspa,nonsense,ckb"},
    }));
  }
#ifdef ALTBASE_SEPARATE_PRIVACY_MODULES
  if (parsed.has_value() && (parsed->method == "privacyLightWallet" || parsed->method == "privacyScope")) {
    const auto coin = parsed->params.find("coin");
    if (coin != parsed->params.end() && coin->second == "zano") {
      *release = altbase_zano_wallet_free;
      return altbase_zano_wallet_request(line.c_str());
    }
    if (coin != parsed->params.end() && coin->second == "epic") {
      *release = altbase_epic_wallet_free;
      return altbase_epic_wallet_request(line.c_str());
    }
    if (coin != parsed->params.end() && coin->second == "monero") {
      return dispatch_monero_wallet(line, parsed->id, release);
    }
  }
#endif

  using RequestFunction = char* (*)(const char*);
  using FreeFunction = void (*)(char*);
  struct WalletModuleApi {
    RequestFunction request;
    FreeFunction free;
  };
  static const std::map<std::string, WalletModuleApi> utxo_modules = {
    {"bitcoincash", {altbase_bitcoincash_wallet_request, altbase_bitcoincash_wallet_free}},
    {"digibyte", {altbase_digibyte_wallet_request, altbase_digibyte_wallet_free}},
    {"peercoin", {altbase_peercoin_wallet_request, altbase_peercoin_wallet_free}},
    {"bitcoin", {altbase_bitcoin_wallet_request, altbase_bitcoin_wallet_free}},
    {"bitcoin2", {altbase_bitcoin2_wallet_request, altbase_bitcoin2_wallet_free}},
    {"bitcoincashii", {altbase_bitcoincashii_wallet_request, altbase_bitcoincashii_wallet_free}},
    {"firo", {altbase_firo_wallet_request, altbase_firo_wallet_free}},
    {"btgs", {altbase_btgs_wallet_request, altbase_btgs_wallet_free}},
    {"capstash", {altbase_capstash_wallet_request, altbase_capstash_wallet_free}},
    {"hypercoin", {altbase_hypercoin_wallet_request, altbase_hypercoin_wallet_free}},
    {"mydogecoin", {altbase_mydogecoin_wallet_request, altbase_mydogecoin_wallet_free}},
    {"pepecoin", {altbase_pepecoin_wallet_request, altbase_pepecoin_wallet_free}},
    {"kerrigan", {altbase_kerrigan_wallet_request, altbase_kerrigan_wallet_free}},
    {"scash", {altbase_scash_wallet_request, altbase_scash_wallet_free}},
    {"litecoinii", {altbase_litecoinii_wallet_request, altbase_litecoinii_wallet_free}},
    {"neoxa", {altbase_neoxa_wallet_request, altbase_neoxa_wallet_free}},
    {"terracoin", {altbase_terracoin_wallet_request, altbase_terracoin_wallet_free}},
    {"junkcoin", {altbase_junkcoin_wallet_request, altbase_junkcoin_wallet_free}},
    {"raptoreum", {altbase_raptoreum_wallet_request, altbase_raptoreum_wallet_free}},
    {"pearl", {altbase_pearl_wallet_request, altbase_pearl_wallet_free}},
  };
  static const std::map<std::string, WalletModuleApi> node_modules = {
    {"bitcoincash", {altbase_bitcoincash_node_request, altbase_bitcoincash_node_free}},
    {"digibyte", {altbase_digibyte_node_request, altbase_digibyte_node_free}},
    {"peercoin", {altbase_peercoin_node_request, altbase_peercoin_node_free}},
    {"nexa", {altbase_nexa_node_request, altbase_nexa_node_free}},
    {"zcash", {altbase_zcash_node_request, altbase_zcash_node_free}},
    {"bitcoin", {altbase_bitcoin_node_request, altbase_bitcoin_node_free}},
    {"bitcoin2", {altbase_bitcoin2_node_request, altbase_bitcoin2_node_free}},
    {"bitcoincashii", {altbase_bitcoincashii_node_request, altbase_bitcoincashii_node_free}},
    {"firo", {altbase_firo_node_request, altbase_firo_node_free}},
    {"btgs", {altbase_btgs_node_request, altbase_btgs_node_free}},
    {"capstash", {altbase_capstash_node_request, altbase_capstash_node_free}},
    {"hypercoin", {altbase_hypercoin_node_request, altbase_hypercoin_node_free}},
    {"mydogecoin", {altbase_mydogecoin_node_request, altbase_mydogecoin_node_free}},
    {"pepecoin", {altbase_pepecoin_node_request, altbase_pepecoin_node_free}},
    {"kerrigan", {altbase_kerrigan_node_request, altbase_kerrigan_node_free}},
    {"scash", {altbase_scash_node_request, altbase_scash_node_free}},
    {"litecoinii", {altbase_litecoinii_node_request, altbase_litecoinii_node_free}},
    {"neoxa", {altbase_neoxa_node_request, altbase_neoxa_node_free}},
    {"terracoin", {altbase_terracoin_node_request, altbase_terracoin_node_free}},
    {"junkcoin", {altbase_junkcoin_node_request, altbase_junkcoin_node_free}},
    {"raptoreum", {altbase_raptoreum_node_request, altbase_raptoreum_node_free}},
    {"pearl", {altbase_pearl_node_request, altbase_pearl_node_free}},
    {"zano", {altbase_zano_node_request, altbase_zano_node_free}},
    {"epic", {altbase_epic_node_request, altbase_epic_node_free}},
    {"quai", {altbase_quai_node_request, altbase_quai_node_free}},
    {"xgr", {altbase_xgr_node_request, altbase_xgr_node_free}},
    {"qubic", {altbase_qubic_node_request, altbase_qubic_node_free}},
    {"kaspa", {altbase_kaspa_node_request, altbase_kaspa_node_free}},
    {"nonsense", {altbase_nonsense_node_request, altbase_nonsense_node_free}},
    {"ckb", {altbase_ckb_node_request, altbase_ckb_node_free}},
  };
  if (parsed.has_value() && parsed->method == "coinNodeRequest") {
    const auto coin = parsed->params.find("coin");
    if (coin != parsed->params.end()) {
      const auto module = node_modules.find(coin->second);
      if (module != node_modules.end()) {
        *release = module->second.free;
        return module->second.request(line.c_str());
      }
    }
    *release = bridge_free;
    return copy_response(altbase::error_response(
      parsed->id,
      "unsupported_node_module",
      "node request does not identify an installed coin module"));
  }
  static const std::set<std::string> coin_methods = {
    "validateAddress",
    "addressVariantsFromLegacy",
    "addressToScript",
    "deriveAddress",
    "deriveWif",
    "signTransaction",
  };
  if (parsed.has_value() && coin_methods.contains(parsed->method)) {
    const auto coin = parsed->params.find("coin");
    if (coin != parsed->params.end()) {
      const auto module = utxo_modules.find(coin->second);
      if (module != utxo_modules.end()) {
        *release = module->second.free;
        return module->second.request(line.c_str());
      }
    }
    *release = bridge_free;
    return copy_response(altbase::error_response(
      parsed->id,
      "unsupported_coin_module",
      "coin-specific request does not identify an installed wallet module"));
  }
  static const std::set<std::string> vault_methods = {
    "health", "generatePhrase", "validatePhrase", "createWalletSecret",
    "verifyWalletPassword", "decryptWalletSecret",
  };
  if (parsed.has_value() && vault_methods.contains(parsed->method)) {
    *release = altbase_wallet_vault_free;
    return altbase_wallet_vault_request(line.c_str());
  }
  if (parsed.has_value() && parsed->method == "signTransaction") {
    *release = altbase_utxo_signer_free;
    return altbase_utxo_signer_request(line.c_str());
  }
  if (parsed.has_value() && (parsed->method == "estimateFee" || parsed->method == "planTransaction")) {
    *release = altbase_utxo_planner_free;
    return altbase_utxo_planner_request(line.c_str());
  }
  if (parsed.has_value() && (parsed->method == "deriveAddress" || parsed->method == "deriveWif")) {
    *release = altbase_utxo_derivation_free;
    return altbase_utxo_derivation_request(line.c_str());
  }
  *release = altbase_utxo_address_free;
  return altbase_utxo_address_request(line.c_str());
}
}

int main(int argc, char** argv) {
  if (!is_bridge_launch(argc, argv)) {
    show_manual_launch_message();
    return 0;
  }

  std::string line;

  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;

    void (*release)(char*) = bridge_free;
    char* response = dispatch_request(line, &release);
    if (response == nullptr) {
      std::cout << "{\"id\":\"\",\"ok\":false,\"error\":{\"code\":\"core_error\",\"message\":\"native module returned no response\"}}" << std::endl;
      continue;
    }
    std::cout << response << std::endl;
    release(response);
  }

  return 0;
}
