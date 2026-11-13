#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
  if (argc != 2 || std::strcmp(argv[1], "--altbase-wallet-bridge") != 0) return 64;
#ifdef _WIN32
  wchar_t executable[32768];
  const auto size = GetModuleFileNameW(nullptr, executable, 32768);
  if (!size || size >= 32768) return 65;
  const auto file = std::filesystem::path(executable).parent_path() / L"altbase_wallet_vault.dll";
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  auto library = LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  auto symbol = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(library, name)); };
#else
#ifdef __APPLE__
  const auto file = std::filesystem::path(argv[0]).parent_path() / "libaltbase_wallet_vault.dylib";
#else
  const auto file = std::filesystem::path(argv[0]).parent_path() / "libaltbase_wallet_vault.so";
#endif
  auto library = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
  auto symbol = [&](const char* name) { return dlsym(library, name); };
#endif
  if (!library) return 65;
  const auto request = reinterpret_cast<char* (*)(const char*)>(symbol("altbase_wallet_vault_request"));
  const auto release = reinterpret_cast<void (*)(char*)>(symbol("altbase_wallet_vault_free"));
  if (!request || !release) return 66;
  std::string line;
  char c;
  while (std::cin.get(c)) {
    if (c != '\n') {
      if (c == '\0' || line.size() >= 2 * 1024 * 1024) return 67;
      line.push_back(c);
      continue;
    }
    char* response = request(line.c_str());
    std::fill(line.begin(), line.end(), '\0'); line.clear();
    if (!response) return 68;
    std::cout << response << '\n' << std::flush;
    volatile char* erase = response;
    for (size_t i = 0, size = std::strlen(response); i < size; ++i) erase[i] = 0;
    release(response);
    if (!std::cout) return 69;
  }
  return 0;
}
