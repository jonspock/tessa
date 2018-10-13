// Copyright (c) 2018 Jon Spock
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include <sodium.h>

namespace ed25519 {
  
void CKey::Sign(const uint256& hash, std::vector<uint8_t>& vchSig) const {
    vchSig.resize(crypto_sign_BYTES + 32);
    unsigned long long signed_message_len;
    crypto_sign(&vchSig[0], &signed_message_len, hash.begin(), 32, sk);
}

}
