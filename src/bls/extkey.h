#pragma once

#include <vector>

namespace ecdsa {

  class ChainCode;
  class CKey;

  struct CExtKey {
    uint8_t nDepth;
    uint8_t vchFingerprint[4];
    unsigned int nChild;
    ChainCode chaincode;
    CKey key;

    friend bool operator==(const CExtKey& a, const CExtKey& b) {
      return a.nDepth == b.nDepth && memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], sizeof(vchFingerprint)) == 0 &&
        a.nChild == b.nChild && a.chaincode == b.chaincode && a.key == b.key;
    }
    
    void Encode(uint8_t code[BIP32_EXTKEY_SIZE]) const;
    void Decode(const uint8_t code[BIP32_EXTKEY_SIZE]);
    bool Derive(CExtKey& out, unsigned int nChild) const;
    CExtPubKey Neuter() const;
    void SetMaster(const uint8_t* seed, unsigned int nSeedLen);
};

}  // namespace bls
