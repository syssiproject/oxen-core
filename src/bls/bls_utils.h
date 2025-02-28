#pragma once

#include <mcl/op.hpp>

#include "crypto/eth.h"

namespace bls {

class PublicKey;
class Signature;

};  // namespace bls

namespace bls_utils {

inline constexpr int BLS_MODE_BINARY = mcl::IoSerialize | mcl::IoBigEndian;

// Must be called to initialize the library before use.  This function is safe to call multiple
// times (i.e. only the first call does anything), and is thread-safe.
void init();

eth::bls_public_key to_crypto_pubkey(const bls::PublicKey& publicKey);
bls::PublicKey from_crypto_pubkey(const eth::bls_public_key& pk);

eth::bls_signature to_crypto_signature(const bls::Signature& sig);
bls::Signature from_crypto_signature(const eth::bls_signature& sig);
}  // namespace bls_utils
