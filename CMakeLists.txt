cmake_minimum_required(VERSION 3.24)

project(AltbaseNativeCore VERSION 0.1.4 LANGUAGES CXX)

include(FetchContent)

if (APPLE)
  set(ZANO_MACOS_FRAMEWORK_PLATFORM "macos-arm64_x86_64" CACHE STRING "Zano macOS xcframework platform slice")
  set(ZANO_MACOS_INSTALL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/zano_native_lib/_install_macosx")
  set(ZANO_MACOS_BOOST_ROOT "${ZANO_MACOS_INSTALL_ROOT}/libboost.xcframework/${ZANO_MACOS_FRAMEWORK_PLATFORM}")
  set(ZANO_MACOS_OPENSSL_ROOT "${ZANO_MACOS_INSTALL_ROOT}/libopenssl.xcframework/${ZANO_MACOS_FRAMEWORK_PLATFORM}")
  set(ZANO_MACOS_ICONV_ROOT "${ZANO_MACOS_INSTALL_ROOT}/libiconv.xcframework/${ZANO_MACOS_FRAMEWORK_PLATFORM}")
  set(ZANO_MACOS_PLAIN_WALLET_LIB "${ZANO_MACOS_INSTALL_ROOT}/libzano-plain-wallet.xcframework/${ZANO_MACOS_FRAMEWORK_PLATFORM}/libzano-plain-wallet.a")
elseif (NOT WIN32)
  find_package(OpenSSL REQUIRED)
  find_package(CURL REQUIRED)
endif()

set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_ECDH OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  secp256k1
  GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
  GIT_TAG v0.7.1
)
FetchContent_MakeAvailable(secp256k1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(altbase_core_bridge
  src/main.cpp
  src/address_validation.cpp
  src/native_http.cpp
  src/privacy_light_wallet.cpp
  src/protocol.cpp
  src/tx_planning.cpp
  src/tx_signing.cpp
  src/wallet_derivation.cpp
  src/wallet_secret.cpp
  src/wallet_core.cpp
)

target_link_libraries(altbase_core_bridge PRIVATE secp256k1)
if (WIN32)
  target_link_libraries(altbase_core_bridge PRIVATE bcrypt winhttp)
elseif(APPLE)
  target_link_libraries(altbase_core_bridge PRIVATE "-lcurl")
else()
  target_link_libraries(altbase_core_bridge PRIVATE OpenSSL::Crypto CURL::libcurl ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(altbase_core_bridge PRIVATE
  ALTBASE_CORE_VERSION=\"${PROJECT_VERSION}\"
)

set(ZANO_NATIVE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/zano_native_lib")
set(ZANO_SOURCE_ROOT "${ZANO_NATIVE_ROOT}/Zano")
set(ZANO_BUILD_ROOT "${ZANO_SOURCE_ROOT}/build/altbase-vs2022-x64")
set(ZANO_RELEASE_SRC "${ZANO_BUILD_ROOT}/src/Release")
set(ZANO_RELEASE_CONTRIB "${ZANO_BUILD_ROOT}/contrib")
set(ZANO_LINUX_BUILD_ROOT "${ZANO_SOURCE_ROOT}/build/altbase-linux-x64")
set(ZANO_LINUX_SRC "${ZANO_LINUX_BUILD_ROOT}/src")
set(ZANO_LINUX_CONTRIB "${ZANO_LINUX_BUILD_ROOT}/contrib")

if (WIN32 AND EXISTS "${ZANO_RELEASE_SRC}/currency_core.lib")
  target_compile_definitions(altbase_core_bridge PRIVATE
    STATICLIB
    DISABLE_TOR
    USE_OPEN_SSL_FOR_ECDSA
    NOMINMAX
  )
  target_include_directories(altbase_core_bridge PRIVATE
    "${ZANO_SOURCE_ROOT}/src"
    "${ZANO_SOURCE_ROOT}/contrib"
    "${ZANO_SOURCE_ROOT}/contrib/eos_portable_archive"
    "${ZANO_SOURCE_ROOT}/contrib/epee/include"
    "${ZANO_SOURCE_ROOT}/contrib/jwt-cpp/include"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/include"
    "${ZANO_NATIVE_ROOT}/_install_windows/openssl/x86_64/include"
    "${ZANO_BUILD_ROOT}/version"
    "${ZANO_BUILD_ROOT}/contrib/zlib"
  )
  target_link_directories(altbase_core_bridge PRIVATE
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/openssl/x86_64/lib"
  )
  target_link_libraries(altbase_core_bridge PRIVATE
    "${ZANO_RELEASE_SRC}/wallet.lib"
    "${ZANO_RELEASE_SRC}/rpc.lib"
    "${ZANO_RELEASE_SRC}/currency_core.lib"
    "${ZANO_RELEASE_SRC}/crypto.lib"
    "${ZANO_RELEASE_SRC}/common.lib"
    "${ZANO_RELEASE_CONTRIB}/db/liblmdb/Release/lmdb.lib"
    "${ZANO_RELEASE_CONTRIB}/db/libmdbx/Release/mdbx.lib"
    "${ZANO_RELEASE_CONTRIB}/zlib/Release/zlibstatic.lib"
    "${ZANO_RELEASE_CONTRIB}/ethereum/libethash/Release/ethash.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/openssl/x86_64/lib/libcrypto.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/openssl/x86_64/lib/libssl.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_system-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_filesystem-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_thread-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_timer-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_date_time-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_chrono-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_regex-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_serialization-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_atomic-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_program_options-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_locale-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_log-vc143-mt-x64-1_84.lib"
    "${ZANO_NATIVE_ROOT}/_install_windows/boost/x86_64/lib/libboost_log_setup-vc143-mt-x64-1_84.lib"
    bcrypt
    crypt32
    ntdll
    ws2_32
    mswsock
    iphlpapi
  )
elseif(UNIX AND NOT APPLE AND EXISTS "${ZANO_LINUX_SRC}/libcurrency_core.a")
  find_package(Threads REQUIRED)
  find_package(Boost 1.74 REQUIRED COMPONENTS
    system
    filesystem
    locale
    thread
    timer
    date_time
    chrono
    regex
    serialization
    atomic
    program_options
    log
  )
  target_compile_definitions(altbase_core_bridge PRIVATE
    STATICLIB
    DISABLE_TOR
    DISABLE_PFR_SERIALIZATION_SELFCHECK
    USE_OPEN_SSL_FOR_ECDSA
  )
  target_include_directories(altbase_core_bridge PRIVATE
    "${ZANO_SOURCE_ROOT}/src"
    "${ZANO_SOURCE_ROOT}/contrib"
    "${ZANO_SOURCE_ROOT}/contrib/eos_portable_archive"
    "${ZANO_SOURCE_ROOT}/contrib/epee/include"
    "${ZANO_SOURCE_ROOT}/contrib/jwt-cpp/include"
    "${ZANO_LINUX_BUILD_ROOT}/version"
    "${ZANO_LINUX_BUILD_ROOT}/contrib/zlib"
  )
  target_link_libraries(altbase_core_bridge PRIVATE
    "${ZANO_LINUX_SRC}/libwallet.a"
    "${ZANO_LINUX_SRC}/librpc.a"
    "${ZANO_LINUX_SRC}/libcurrency_core.a"
    "${ZANO_LINUX_SRC}/libcrypto.a"
    "${ZANO_LINUX_SRC}/libcommon.a"
    "${ZANO_LINUX_CONTRIB}/db/liblmdb/liblmdb.a"
    "${ZANO_LINUX_CONTRIB}/db/libmdbx/libmdbx.a"
    "${ZANO_LINUX_CONTRIB}/zlib/libz.a"
    "${ZANO_LINUX_CONTRIB}/ethereum/libethash/libethash.a"
    "${ZANO_LINUX_CONTRIB}/miniupnp/miniupnpc/libminiupnpc.a"
    Threads::Threads
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
  )
elseif(APPLE AND EXISTS "${ZANO_MACOS_PLAIN_WALLET_LIB}")
  find_package(Threads REQUIRED)
  set(ZANO_MACOS_VERSION_DIR "${CMAKE_BINARY_DIR}/zano-version")
  file(MAKE_DIRECTORY "${ZANO_MACOS_VERSION_DIR}")
  set(VERSION "altbase-macos")
  configure_file("${ZANO_SOURCE_ROOT}/src/version.h.in" "${ZANO_MACOS_VERSION_DIR}/version.h" @ONLY)
  target_compile_definitions(altbase_core_bridge PRIVATE
    STATICLIB
    DISABLE_TOR
    USE_OPEN_SSL_FOR_ECDSA
  )
  target_include_directories(altbase_core_bridge PRIVATE
    "${ZANO_SOURCE_ROOT}/src"
    "${ZANO_SOURCE_ROOT}/contrib"
    "${ZANO_SOURCE_ROOT}/contrib/eos_portable_archive"
    "${ZANO_SOURCE_ROOT}/contrib/epee/include"
    "${ZANO_SOURCE_ROOT}/contrib/jwt-cpp/include"
    "${ZANO_MACOS_BOOST_ROOT}/Headers"
    "${ZANO_MACOS_OPENSSL_ROOT}/Headers"
    "${ZANO_MACOS_ICONV_ROOT}/Headers"
    "${ZANO_MACOS_VERSION_DIR}"
  )
  target_link_libraries(altbase_core_bridge PRIVATE
    "${ZANO_MACOS_PLAIN_WALLET_LIB}"
    Threads::Threads
    "-framework Foundation"
    "-framework Security"
    "-framework CoreFoundation"
    "-framework SystemConfiguration"
    "-liconv"
  )
else()
  message(WARNING "Zano native libraries were not found; Zano privacy wallet will remain not-ready until Zano source is configured and built.")
endif()

target_include_directories(altbase_core_bridge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if (MSVC)
  target_compile_options(altbase_core_bridge PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(altbase_core_bridge PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(altbase_core_bridge PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
