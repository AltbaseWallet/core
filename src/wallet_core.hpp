#pragma once

#include "protocol.hpp"

#include <map>
#include <string>

namespace altbase {

class WalletCore {
 public:
  std::map<std::string, std::string> handle(const Request& request);
};

}  // namespace altbase
