#pragma once

#ifdef _WIN32
#define ALTBASE_MONERO_WALLET_CALL __cdecl
#else
#define ALTBASE_MONERO_WALLET_CALL
#endif

using AltbaseMoneroWalletRequest = char* (ALTBASE_MONERO_WALLET_CALL*)(const char* request);
using AltbaseMoneroWalletFree = void (ALTBASE_MONERO_WALLET_CALL*)(char* value);
