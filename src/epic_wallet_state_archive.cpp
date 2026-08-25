#include "epic_wallet_state_archive.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace altbase {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uintmax_t kMaxArchiveBytes = 12ULL * 1024ULL * 1024ULL;
constexpr const char* kArchiveMagic = "ALTBASE_EPIC_WALLET_ARCHIVE_V1";

std::string get_param(const std::map<std::string, std::string>& params, const std::string& key) {
  const auto it = params.find(key);
  return it == params.end() ? "" : it->second;
}

bool scope_is_safe(const std::string& scope) {
  return !scope.empty() && scope.size() <= 128 && std::all_of(scope.begin(), scope.end(), [](unsigned char ch) {
    return std::isalnum(ch) != 0 || ch == '-' || ch == '_';
  });
}

std::filesystem::path work_dir(
  const std::map<std::string, std::string>& params,
  const std::string& scope
) {
  const auto configured = get_param(params, "userDataDir");
  const auto base = configured.empty()
    ? std::filesystem::current_path()
    : std::filesystem::path(configured);
  return base / "epic-light" / ("altbase-epic-v1-" + scope);
}

std::filesystem::path wallet_seed_path(const std::filesystem::path& root) {
  return root / "wallet_data" / "wallet.seed";
}

std::uintmax_t safe_file_size(const std::filesystem::path& path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  return ec ? 0 : size;
}

bool read_file(const std::filesystem::path& path, Bytes& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  return static_cast<bool>(file) || file.eof();
}

bool write_file(const std::filesystem::path& path, const Bytes& bytes) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  file.flush();
  return static_cast<bool>(file);
}

std::string base64_encode(const Bytes& data) {
  static constexpr char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const size_t remaining = data.size() - i;
    const std::uint32_t a = data[i];
    const std::uint32_t b = remaining > 1 ? data[i + 1] : 0;
    const std::uint32_t c = remaining > 2 ? data[i + 2] : 0;
    const std::uint32_t triple = (a << 16U) | (b << 8U) | c;
    out.push_back(alphabet[(triple >> 18U) & 0x3fU]);
    out.push_back(alphabet[(triple >> 12U) & 0x3fU]);
    out.push_back(remaining > 1 ? alphabet[(triple >> 6U) & 0x3fU] : '=');
    out.push_back(remaining > 2 ? alphabet[triple & 0x3fU] : '=');
  }
  return out;
}

std::optional<Bytes> base64_decode(const std::string& text) {
  auto value_of = [](char ch) -> int {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+' || ch == '-') return 62;
    if (ch == '/' || ch == '_') return 63;
    return -1;
  };

  Bytes out;
  std::uint32_t bits = 0;
  unsigned bit_count = 0;
  for (const auto ch : text) {
    if (ch == '=') break;
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) continue;
    const auto value = value_of(ch);
    if (value < 0) return std::nullopt;
    bits = (bits << 6U) | static_cast<std::uint32_t>(value);
    bit_count += 6;
    if (bit_count >= 8) {
      bit_count -= 8;
      out.push_back(static_cast<std::uint8_t>((bits >> bit_count) & 0xff));
      bits = bit_count == 0 ? 0U : bits & ((1U << bit_count) - 1U);
    }
  }
  return out;
}

bool archive_key_is_safe(const std::string& key) {
  if (key.empty() || key.front() == '/' || key.front() == '\\') return false;
  if (key.find('\\') != std::string::npos || key.find(':') != std::string::npos) return false;
  if (key == "altbase.restore-scan.done") return true;
  if (key.rfind("wallet_data/", 0) != 0) return false;

  size_t start = 0;
  while (start <= key.size()) {
    const auto end = key.find('/', start);
    const auto part = key.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

std::optional<std::string> relative_archive_key(
  const std::filesystem::path& root,
  const std::filesystem::path& file
) {
  std::error_code ec;
  const auto relative = std::filesystem::relative(file, root, ec);
  if (ec) return std::nullopt;
  const auto key = relative.generic_string();
  return archive_key_is_safe(key) ? std::optional<std::string>(key) : std::nullopt;
}

std::vector<std::filesystem::path> backup_paths(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> paths = {
    root / "altbase.restore-scan.done",
    wallet_seed_path(root),
    root / "wallet_data" / "db" / "sqlite" / "epic.db",
    root / "wallet_data" / "db" / "sqlite" / "epic.db-wal",
    root / "wallet_data" / "db" / "sqlite" / "epic.db-shm",
  };

  const auto saved = root / "wallet_data" / "saved_txs";
  std::error_code ec;
  if (std::filesystem::exists(saved, ec) && !ec) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(saved, ec)) {
      if (ec) break;
      std::error_code entry_error;
      if (entry.is_regular_file(entry_error) && !entry_error) paths.push_back(entry.path());
    }
  }
  return paths;
}

bool wallet_is_prepared(const std::filesystem::path& root) {
  return safe_file_size(wallet_seed_path(root)) > 0
    && safe_file_size(root / "wallet_data" / "db" / "sqlite" / "epic.db") >= 4096;
}

size_t count_token(const Bytes& bytes, const std::string& token) {
  if (bytes.size() < token.size() || token.empty()) return 0;
  size_t count = 0;
  auto it = bytes.begin();
  while (it != bytes.end()) {
    it = std::search(it, bytes.end(), token.begin(), token.end());
    if (it == bytes.end()) break;
    ++count;
    ++it;
  }
  return count;
}

size_t count_token_in_file(const std::filesystem::path& path, const std::string& token) {
  Bytes bytes;
  return read_file(path, bytes) ? count_token(bytes, token) : 0;
}

std::optional<std::uintmax_t> parse_unsigned(const std::string& text) {
  if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
    return std::isdigit(ch) != 0;
  })) return std::nullopt;
  try {
    return static_cast<std::uintmax_t>(std::stoull(text));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::uintmax_t> marker_height(const std::string& text) {
  constexpr const char* prefix = "scannedHeight=";
  size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    auto line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind(prefix, 0) == 0) return parse_unsigned(line.substr(std::char_traits<char>::length(prefix)));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return std::nullopt;
}

std::optional<std::uintmax_t> marker_height_from_file(const std::filesystem::path& path) {
  Bytes bytes;
  if (!read_file(path, bytes)) return std::nullopt;
  return marker_height(std::string(bytes.begin(), bytes.end()));
}

std::uintmax_t local_backup_size(const std::filesystem::path& root) {
  std::uintmax_t total = 0;
  for (const auto& path : backup_paths(root)) total += safe_file_size(path);
  return total;
}

struct Backup {
  std::string name;
  std::string blob;
  std::uintmax_t size = 0;
};

std::optional<Backup> create_backup(const std::filesystem::path& root, const std::string& scope) {
  if (!wallet_is_prepared(root)) return std::nullopt;

  Bytes archive;
  auto append = [&archive](const std::string& value) {
    archive.insert(archive.end(), value.begin(), value.end());
  };
  append(std::string(kArchiveMagic) + "\n");

  std::uintmax_t total_size = 0;
  bool has_seed = false;
  bool has_db = false;
  for (const auto& path : backup_paths(root)) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) continue;
    const auto key = relative_archive_key(root, path);
    if (!key.has_value()) continue;

    Bytes bytes;
    if (!read_file(path, bytes) || bytes.empty()) continue;
    if (total_size + bytes.size() > kMaxArchiveBytes) return std::nullopt;
    total_size += bytes.size();
    has_seed = has_seed || *key == "wallet_data/wallet.seed";
    has_db = has_db || *key == "wallet_data/db/sqlite/epic.db";
    append("FILE\t" + *key + "\t" + std::to_string(bytes.size()) + "\t" + base64_encode(bytes) + "\n");
  }

  if (!has_seed || !has_db || total_size == 0) return std::nullopt;
  return Backup{
    "altbase-epic-wallet-v1-" + scope + ".archive",
    base64_encode(archive),
    total_size,
  };
}

struct ArchivedFile {
  std::string key;
  Bytes bytes;
};

bool replace_file(const std::filesystem::path& target, const Bytes& bytes) {
  std::filesystem::create_directories(target.parent_path());
  auto temporary = target;
  temporary += ".importtmp";
  if (!write_file(temporary, bytes)) return false;

  std::error_code ec;
  std::filesystem::rename(temporary, target, ec);
  if (!ec) return true;
  std::filesystem::remove(target, ec);
  ec.clear();
  std::filesystem::rename(temporary, target, ec);
  if (!ec) return true;
  std::filesystem::remove(temporary, ec);
  return false;
}

}  // namespace

bool import_epic_wallet_state_archive(
  const std::map<std::string, std::string>& params,
  const std::string& scope
) {
  if (!scope_is_safe(scope)) return false;
  auto blob = get_param(params, "nativeWalletFileBlob");
  if (blob.empty()) blob = get_param(params, "cachedWalletState");
  if (blob.empty()) return false;

  const auto decoded = base64_decode(blob);
  if (!decoded.has_value() || decoded->empty()) return false;
  std::istringstream input(std::string(decoded->begin(), decoded->end()));
  std::string line;
  if (!std::getline(input, line) || line != kArchiveMagic) return false;

  std::vector<ArchivedFile> files;
  std::uintmax_t total_size = 0;
  bool has_seed = false;
  bool has_db = false;
  size_t archived_transaction_count = 0;
  std::optional<std::uintmax_t> archived_height;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    const auto first = line.find('\t');
    const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
    const auto third = second == std::string::npos ? std::string::npos : line.find('\t', second + 1);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos) return false;
    if (line.substr(0, first) != "FILE") return false;

    const auto key = line.substr(first + 1, second - first - 1);
    if (!archive_key_is_safe(key)) return false;
    const auto expected_size = parse_unsigned(line.substr(second + 1, third - second - 1));
    const auto bytes = base64_decode(line.substr(third + 1));
    if (!expected_size.has_value() || !bytes.has_value() || bytes->size() != *expected_size) return false;
    if (total_size + bytes->size() > kMaxArchiveBytes) return false;

    total_size += bytes->size();
    has_seed = has_seed || key == "wallet_data/wallet.seed";
    if (key == "wallet_data/db/sqlite/epic.db") {
      has_db = true;
      archived_transaction_count = count_token(*bytes, "\"tx_type\":\"Tx");
    } else if (key == "altbase.restore-scan.done") {
      archived_height = marker_height(std::string(bytes->begin(), bytes->end()));
    }
    files.push_back({key, *bytes});
  }
  if (!has_seed || !has_db || files.empty()) return false;

  const auto root = work_dir(params, scope);
  if (wallet_is_prepared(root)) {
    const auto local_height = marker_height_from_file(root / "altbase.restore-scan.done");
    const auto local_transaction_count = count_token_in_file(
      root / "wallet_data" / "db" / "sqlite" / "epic.db",
      "\"tx_type\":\"Tx"
    );
    if (local_height.has_value()) {
      // Never roll a prepared wallet back to an older (or unversioned)
      // encrypted archive. Transaction history is merged separately by the
      // renderer, while replacing a newer native DB forces a costly historic
      // rescan and can temporarily hide valid spendable outputs.
      if (!archived_height.has_value() || *local_height > *archived_height) return false;
      if (
        *local_height == *archived_height
        && local_transaction_count >= archived_transaction_count
        && local_backup_size(root) >= total_size
      ) return false;
    }
  }

  std::filesystem::create_directories(root);
  for (const auto& file : files) {
    if (!replace_file(root / std::filesystem::path(file.key), file.bytes)) return false;
  }
  return true;
}

void attach_epic_wallet_state_archive(
  PrivacyLightWalletResult& result,
  const std::map<std::string, std::string>& params,
  const std::string& scope
) {
  if (!scope_is_safe(scope)) return;
  const auto backup = create_backup(work_dir(params, scope), scope);
  if (!backup.has_value()) return;
  result.native_wallet_file_name = backup->name;
  result.native_wallet_file_blob = backup->blob;
  result.native_wallet_file_size = std::to_string(backup->size);
}

}  // namespace altbase
