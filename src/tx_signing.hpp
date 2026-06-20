#pragma once

#include <map>
#include <string>

namespace altbase {

struct SignedTransactionResult {
  std::string tx_hex;
};

SignedTransactionResult sign_utxo_transaction(const std::map<std::string, std::string>& params);

}  // namespace altbase
