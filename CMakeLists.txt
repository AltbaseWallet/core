cmake_minimum_required(VERSION 3.20)
project(AltbaseWalletVault VERSION 0.1.13 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Compile the supplied source directly, without generating or patching a copy.
add_library(altbase_wallet_vault SHARED
  src/wallet_secret.cpp src/protocol.cpp
  src/wallet_vault.cpp src/wallet_vault_module.cpp)
target_include_directories(altbase_wallet_vault PRIVATE src)
target_compile_definitions(altbase_wallet_vault PRIVATE
  ALTBASE_WALLET_VAULT_EXPORTS ALTBASE_CORE_VERSION="${PROJECT_VERSION}")
set_target_properties(altbase_wallet_vault PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
  BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
if(WIN32)
  set_target_properties(altbase_wallet_vault PROPERTIES PREFIX "")
  target_link_libraries(altbase_wallet_vault PRIVATE bcrypt winhttp)
  if(MINGW)
    target_link_options(altbase_wallet_vault PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
else()
  find_package(OpenSSL 3.0 REQUIRED)
  target_link_libraries(altbase_wallet_vault PRIVATE OpenSSL::Crypto)
endif()

add_executable(altbase_vault_bridge src/vault_bridge.cpp)
add_dependencies(altbase_vault_bridge altbase_wallet_vault)
if(WIN32 AND MINGW)
  target_link_options(altbase_vault_bridge PRIVATE -static -static-libgcc -static-libstdc++)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(altbase_vault_bridge PRIVATE dl)
endif()
