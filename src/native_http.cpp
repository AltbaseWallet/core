#include "native_http.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace altbase {
namespace {

struct Handle {
  HINTERNET value = nullptr;
  ~Handle() {
    if (value) WinHttpCloseHandle(value);
  }
  Handle() = default;
  explicit Handle(HINTERNET handle) : value(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  operator HINTERNET() const { return value; }
};

std::wstring utf8_to_wide(const std::string& value) {
  if (value.empty()) return L"";
  const int len = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (len <= 0) throw std::runtime_error("utf8 conversion failed");
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), len);
  return out;
}

std::string wide_to_utf8(const std::wstring& value) {
  if (value.empty()) return "";
  const int len = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (len <= 0) throw std::runtime_error("wide conversion failed");
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), len, nullptr, nullptr);
  return out;
}

struct ParsedUrl {
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool secure = true;
};

ParsedUrl parse_url(const std::string& url) {
  const auto wide = utf8_to_wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwSchemeLength = static_cast<DWORD>(-1);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
    throw std::runtime_error("invalid URL");
  }
  std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  if (parts.dwExtraInfoLength > 0) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  return {
    std::wstring(parts.lpszHostName, parts.dwHostNameLength),
    path.empty() ? L"/" : path,
    parts.nPort,
    parts.nScheme == INTERNET_SCHEME_HTTPS,
  };
}

HttpResponse request(
  const std::string& method,
  const std::string& url,
  const std::string& body,
  unsigned long timeout_ms,
  const wchar_t* content_type = nullptr
) {
  const auto parsed = parse_url(url);
  Handle session(WinHttpOpen(
    L"AltbaseNativeCore/0.1",
    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS,
    0
  ));
  if (!session) throw std::runtime_error("WinHttpOpen failed");

  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  Handle connect(WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0));
  if (!connect) throw std::runtime_error("WinHttpConnect failed");

  const auto wide_method = utf8_to_wide(method);
  Handle req(WinHttpOpenRequest(
    connect,
    wide_method.c_str(),
    parsed.path.c_str(),
    nullptr,
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    parsed.secure ? WINHTTP_FLAG_SECURE : 0
  ));
  if (!req) throw std::runtime_error("WinHttpOpenRequest failed");

  std::wstring headers;
  if (method == "POST") {
    headers = L"Content-Type: ";
    headers += content_type ? content_type : L"application/json";
    headers += L"\r\n";
  }
  const void* body_ptr = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
  const DWORD body_len = static_cast<DWORD>(body.size());
  if (!WinHttpSendRequest(
        req,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<void*>(body_ptr),
        body_len,
        body_len,
        0
      )) {
    throw std::runtime_error("WinHttpSendRequest failed");
  }
  if (!WinHttpReceiveResponse(req, nullptr)) throw std::runtime_error("WinHttpReceiveResponse failed");

  DWORD status = 0;
  DWORD status_size = sizeof(status);
  WinHttpQueryHeaders(
    req,
    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX,
    &status,
    &status_size,
    WINHTTP_NO_HEADER_INDEX
  );

  std::string response_body;
  for (;;) {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(req, &available)) throw std::runtime_error("WinHttpQueryDataAvailable failed");
    if (available == 0) break;
    std::vector<char> chunk(available);
    DWORD read = 0;
    if (!WinHttpReadData(req, chunk.data(), available, &read)) throw std::runtime_error("WinHttpReadData failed");
    response_body.append(chunk.data(), chunk.data() + read);
  }

  return {static_cast<int>(status), response_body, {}};
}

}  // namespace

HttpResponse http_get(const std::string& url, unsigned long timeout_ms) {
  return request("GET", url, "", timeout_ms);
}

HttpResponse http_post_json(const std::string& url, const std::string& body, unsigned long timeout_ms) {
  return request("POST", url, body, timeout_ms);
}

HttpResponse http_post_binary(const std::string& url, const std::string& body, unsigned long timeout_ms) {
  return request("POST", url, body, timeout_ms, L"application/octet-stream");
}

}  // namespace altbase

#else

#include <curl/curl.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace altbase {
namespace {

std::once_flag curl_init_flag;

size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}

HttpResponse request(
  const std::string& method,
  const std::string& url,
  const std::string& body,
  unsigned long timeout_ms,
  const char* content_type = nullptr
) {
  std::call_once(curl_init_flag, []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  });

  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl_easy_init failed");

  std::string response_body;
  struct curl_slist* headers = nullptr;
  if (method == "POST") {
    const std::string header = std::string("Content-Type: ") + (content_type ? content_type : "application/json");
    headers = curl_slist_append(headers, header.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "AltbaseNativeCore/0.1");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (headers) curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) throw std::runtime_error(curl_easy_strerror(rc));

  return {static_cast<int>(status), response_body, {}};
}

}  // namespace

HttpResponse http_get(const std::string& url, unsigned long timeout_ms) {
  return request("GET", url, "", timeout_ms);
}

HttpResponse http_post_json(const std::string& url, const std::string& body, unsigned long timeout_ms) {
  return request("POST", url, body, timeout_ms);
}

HttpResponse http_post_binary(const std::string& url, const std::string& body, unsigned long timeout_ms) {
  return request("POST", url, body, timeout_ms, "application/octet-stream");
}

}  // namespace altbase

#endif
