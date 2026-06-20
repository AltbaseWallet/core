#include "wallet_core.hpp"

#include "address_validation.hpp"
#include "privacy_light_wallet.hpp"
#include "tx_planning.hpp"
#include "tx_signing.hpp"
#include "wallet_derivation.hpp"
#include "wallet_secret.hpp"

#include <stdexcept>
#include <string>

namespace altbase {

std::map<std::string, std::string> WalletCore::handle(const Request& request) {
  if (request.method == "health") {
    return {
      {"service", "altbase-core"},
      {"version", "0.1.0"},
      {"status", "ok"},
      {"methods", "health,generateMnemonic,validateMnemonic,createWalletSecret,verifyWalletPassword,decryptWalletSecret,privacyWalletSecret,privacyLightWallet,deriveAddress,validateAddress,addressToScript,addressVariantsFromLegacy,estimateFee,planTransaction,buildTransaction,signTransaction"},
    };
  }

  if (request.method == "generateMnemonic") {
    return {
      {"mnemonic", generate_bip39_mnemonic()},
    };
  }

  if (request.method == "validateMnemonic") {
    return {
      {"isValid", validate_bip39_mnemonic(request.params.count("mnemonic") ? request.params.at("mnemonic") : "") ? "true" : "false"},
    };
  }

  if (request.method == "createWalletSecret") {
    const auto secret = create_wallet_secret(request.params);
    return {
      {"verifyHash", secret.verify_hash},
      {"verifySalt", secret.verify_salt},
      {"cipherText", secret.cipher_text},
      {"iv", secret.iv},
      {"salt", secret.salt},
    };
  }

  if (request.method == "verifyWalletPassword") {
    return {
      {"isValid", verify_wallet_password(request.params) ? "true" : "false"},
    };
  }

  if (request.method == "decryptWalletSecret") {
    return {
      {"mnemonic", decrypt_wallet_secret(request.params)},
    };
  }

  if (request.method == "privacyWalletSecret") {
    const auto secret = privacy_wallet_secret(request.params);
    return {
      {"enginePassword", secret.engine_password},
      {"scope", secret.scope},
      {"seed", secret.seed},
    };
  }

  if (request.method == "privacyLightWallet") {
    const auto result = privacy_light_wallet(request.params);
    return {
      {"ok", result.ok ? "true" : "false"},
      {"code", result.code},
      {"error", result.error},
      {"address", result.address},
      {"balance", result.balance},
      {"spendable", result.spendable},
      {"txid", result.txid},
      {"amount", result.amount},
      {"fee", result.fee},
      {"transactions", result.transactions},
      {"lastScannedHeight", result.last_scanned_height},
      {"scanState", result.scan_state},
      {"serverStatus", result.server_status},
      {"nativeWalletFileName", result.native_wallet_file_name},
      {"nativeWalletFileBlob", result.native_wallet_file_blob},
      {"nativeWalletFileSize", result.native_wallet_file_size},
    };
  }

  if (request.method == "validateAddress") {
    const auto validation = validate_address(request.params);
    return {
      {"isValid", validation.is_valid ? "true" : "false"},
      {"format", validation.format},
      {"scriptKind", validation.script_kind},
      {"scriptPubKey", validation.script_pub_key},
      {"error", validation.error},
    };
  }

  if (request.method == "addressToScript") {
    const auto validation = validate_address(request.params);
    if (!validation.is_valid) {
      throw std::runtime_error("invalid address: " + validation.error);
    }
    return {
      {"scriptPubKey", validation.script_pub_key},
      {"format", validation.format},
      {"scriptKind", validation.script_kind},
    };
  }

  if (request.method == "addressVariantsFromLegacy") {
    const auto variants = address_variants_from_legacy(request.params);
    std::string encoded;
    for (const auto& variant : variants) {
      if (!encoded.empty()) encoded += "|";
      encoded += variant.id + "," +
                 variant.label + "," +
                 variant.address + "," +
                 variant.script_kind + "," +
                 (variant.alias_of_legacy ? "true" : "false");
    }
    return {
      {"variants", encoded},
    };
  }

  if (request.method == "deriveAddress" || request.method == "derivePrivateKeyWif") {
    const auto material = derive_wallet_material(request.params);
    if (request.method == "deriveAddress") {
      return {
        {"address", material.address},
        {"publicKey", material.public_key_hex},
      };
    }
    return {
      {"privateKeyWif", material.private_key_wif},
      {"address", material.address},
      {"publicKey", material.public_key_hex},
    };
  }

  if (request.method == "signTransaction") {
    const auto signed_tx = sign_utxo_transaction(request.params);
    return {
      {"txHex", signed_tx.tx_hex},
    };
  }

  if (request.method == "estimateFee") {
    const auto fee = estimate_transaction_fee(request.params);
    return {
      {"feeSatoshis", std::to_string(fee.fee_satoshis)},
    };
  }

  if (request.method == "planTransaction") {
    const auto plan = plan_utxo_transaction(request.params);
    return {
      {"amountSatoshis", std::to_string(plan.amount_satoshis)},
      {"feeSatoshis", std::to_string(plan.fee_satoshis)},
      {"inputCount", std::to_string(plan.input_count)},
      {"selectedInputs", plan.selected_inputs},
      {"outputs", plan.outputs},
    };
  }

  if (request.method == "buildTransaction") {
    throw std::runtime_error("method is reserved for native implementation and is not wired yet: " + request.method);
  }

  throw std::runtime_error("unknown method: " + request.method);
}

}  // namespace altbase
