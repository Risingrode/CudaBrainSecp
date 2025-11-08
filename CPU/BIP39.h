// Minimal BIP39/BIP32 CPU-side derivation utilities for BTC
// Notes:
// - Mnemonic/passphrase normalization (NFKD) is NOT implemented here. Provide ASCII or pre-normalized input.
// - PBKDF2-HMAC-SHA512 with 2048 iterations per BIP39.
// - BIP32 master/child private derivation (CKDpriv) with hardened and non-hardened indices.

#pragma once

#include <string>
#include <vector>
#include <stdint.h>
#include "CPU/SECP256k1.h"
#include <unordered_map>

namespace BIP39 {

// === SHA-512 / HMAC-SHA512 / PBKDF2-HMAC-SHA512 ===
void SHA512(const uint8_t* data, size_t len, uint8_t out[64]);
void HMAC_SHA512(const uint8_t* key, size_t keylen, const uint8_t* msg, size_t msglen, uint8_t out[64]);
void PBKDF2_HMAC_SHA512(const std::string& mnemonic, const std::string& passphrase, uint8_t outSeed[64], int iterations = 2048);

// === BIP32 ===
// Return false if IL is invalid (0 or >= n)
bool BIP32_MasterFromSeed(const uint8_t seed[64], Int &k_m, uint8_t chainCode[32]);

// Return false if derived key is invalid
bool BIP32_CKDPriv(const Int &k_par, const uint8_t chainCodePar[32], uint32_t index, Int &k_i, uint8_t chainCodeChild[32], Secp256K1 &secp);

// Derive along a full path (e.g. m/44'/0'/0'/0/0). Path must not include the leading 'm'.
bool DerivePath(const Int &k_m, const uint8_t chainCodeM[32], const std::vector<uint32_t>& path, Int &k_out, uint8_t chainCodeOut[32], Secp256K1 &secp);

// Parse a path string like "m/44'/0'/0'/0/0" into vector of indices (hardened index adds 0x80000000)
bool ParsePath(const std::string &pathStr, std::vector<uint32_t> &out);

// Utility: build a packed list of 32-byte private keys for a batch of mnemonics and a leaf index range.
// For each mnemonic in list, derive [rangeStart, rangeStart+rangeCount) on provided path.
// Returns concatenated array of 32-byte private keys (big-endian) in outPrivKeys.
bool BuildPrivListFromMnemonics(const std::vector<std::string>& mnemonics,
                                const std::string& passphrase,
                                const std::vector<uint32_t>& basePath,
                                uint32_t rangeStart,
                                uint32_t rangeCount,
                                std::vector<uint8_t>& outPrivKeys);

// Optional: BIP39 checksum validation requiring full wordlist order (2048 words)
bool LoadWordlist(const std::string& path, std::vector<std::string>& wl);
bool IsValidMnemonicWithWordlist(const std::vector<std::string>& words, const std::vector<std::string>& wl, const std::unordered_map<std::string,int>& index);

} // namespace BIP39
