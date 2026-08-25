#pragma once

#include "privacy_light_wallet.hpp"

#include <map>
#include <string>

namespace altbase {

// The upstream Epic wallet module intentionally owns only wallet protocol
// operations. Altbase persists its local encrypted wallet files separately so
// restoring the same seed can resume from the last stored height.
bool import_epic_wallet_state_archive(
  const std::map<std::string, std::string>& params,
  const std::string& scope
);

void attach_epic_wallet_state_archive(
  PrivacyLightWalletResult& result,
  const std::map<std::string, std::string>& params,
  const std::string& scope
);

}  // namespace altbase
