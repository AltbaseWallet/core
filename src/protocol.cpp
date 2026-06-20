#include "protocol.hpp"

#include <cctype>
#include <regex>
#include <sstream>

namespace altbase {
namespace {

void skip_ws(const std::string& s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) ++i;
}

bool consume(const std::string& s, size_t& i, char expected) {
  skip_ws(s, i);
  if (i >= s.size() || s[i] != expected) return false;
  ++i;
  return true;
}

std::optional<std::string> parse_string(const std::string& s, size_t& i) {
  skip_ws(s, i);
  if (i >= s.size() || s[i] != '"') return std::nullopt;
  ++i;

  std::string out;
  while (i < s.size()) {
    const char c = s[i++];
    if (c == '"') return out;
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (i >= s.size()) return std::nullopt;
    const char e = s[i++];
    switch (e) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

std::map<std::string, std::string> parse_flat_object(const std::string& s, size_t& i) {
  std::map<std::string, std::string> out;
  if (!consume(s, i, '{')) return out;
  skip_ws(s, i);
  if (i < s.size() && s[i] == '}') {
    ++i;
    return out;
  }

  while (i < s.size()) {
    auto key = parse_string(s, i);
    if (!key || !consume(s, i, ':')) return {};

    skip_ws(s, i);
    std::string value;
    if (i < s.size() && s[i] == '"') {
      auto parsed = parse_string(s, i);
      if (!parsed) return {};
      value = *parsed;
    } else {
      const size_t start = i;
      while (i < s.size() && s[i] != ',' && s[i] != '}') ++i;
      value = s.substr(start, i - start);
      while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.pop_back();
    }
    out[*key] = value;

    skip_ws(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (i < s.size() && s[i] == '}') {
      ++i;
      return out;
    }
    return {};
  }
  return {};
}

}  // namespace

std::optional<Request> parse_request(const std::string& line, std::string& error) {
  std::smatch match;
  static const std::regex id_re(R"json("id"\s*:\s*"([^"]*)")json");
  static const std::regex method_re(R"json("method"\s*:\s*"([^"]+)")json");

  if (!std::regex_search(line, match, id_re)) {
    error = "request must include id and method";
    return std::nullopt;
  }
  Request request;
  request.id = match[1].str();

  if (!std::regex_search(line, match, method_re)) {
    error = "request must include id and method";
    return std::nullopt;
  }
  request.method = match[1].str();

  const auto params_pos = line.find("\"params\"");
  if (params_pos != std::string::npos) {
    auto brace = line.find('{', params_pos);
    if (brace != std::string::npos) request.params = parse_flat_object(line, brace);
  }
  return request;
}

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (const char c : value) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) out << ' ';
        else out << c;
    }
  }
  return out.str();
}

std::string ok_response(const std::string& id, const std::map<std::string, std::string>& fields) {
  std::ostringstream out;
  out << "{\"id\":\"" << json_escape(id) << "\",\"ok\":true,\"result\":{";
  bool first = true;
  for (const auto& [key, value] : fields) {
    if (!first) out << ',';
    first = false;
    out << "\"" << json_escape(key) << "\":\"" << json_escape(value) << "\"";
  }
  out << "}}";
  return out.str();
}

std::string error_response(const std::string& id, const std::string& code, const std::string& message) {
  std::ostringstream out;
  out << "{\"id\":\"" << json_escape(id) << "\",\"ok\":false,\"error\":{"
      << "\"code\":\"" << json_escape(code) << "\","
      << "\"message\":\"" << json_escape(message) << "\"}}";
  return out.str();
}

}  // namespace altbase
