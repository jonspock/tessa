// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypter.h"
#include "crypto/aes.h"
#include "crypto/sha512.h"
#include "hash.h"
#include "init.h"
#include "script/script.h"
#include "script/standard.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

#include "wallet/wallet.h"
#include "wallet/wallettx.h"

#include <boost/signals2/signal.hpp>

using namespace std;
using namespace ecdsa;

int CCrypter::BytesToKeySHA512AES(const std::vector<uint8_t> &chSalt, const SecureString &strKeyData, int count,
                                  uint8_t *key, uint8_t *iv) const {
  // This mimics the behavior of openssl's EVP_BytesToKey with an aes256cbc
  // cipher and sha512 message digest. Because sha512's output size (64b) is
  // greater than the aes256 block size (16b) + aes256 key size (32b), there's
  // no need to process more than once (D_0).
  if (!count || !key || !iv) return 0;

  uint8_t buf[CSHA512::OUTPUT_SIZE];
  CSHA512 di;

  di.Write((const uint8_t *)strKeyData.c_str(), strKeyData.size());
  if (chSalt.size()) di.Write(&chSalt[0], chSalt.size());
  di.Finalize(buf);

  for (int i = 0; i != count - 1; i++) di.Reset().Write(buf, sizeof(buf)).Finalize(buf);

  memcpy(key, buf, WALLET_CRYPTO_KEY_SIZE);
  memcpy(iv, buf + WALLET_CRYPTO_KEY_SIZE, WALLET_CRYPTO_IV_SIZE);
  memory_cleanse(buf, sizeof(buf));
  return WALLET_CRYPTO_KEY_SIZE;
}

bool CCrypter::SetKeyFromPassphrase(const SecureString &strKeyData, const std::vector<uint8_t> &chSalt,
                                    const uint32_t nRounds, const uint32_t nDerivationMethod) {
  if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE) return false;

  int i = 0;
  if (nDerivationMethod == 0) i = BytesToKeySHA512AES(chSalt, strKeyData, nRounds, vchKey.data(), vchIV.data());

  if (i != (int)WALLET_CRYPTO_KEY_SIZE) {
    memory_cleanse(vchKey.data(), vchKey.size());
    memory_cleanse(vchIV.data(), vchIV.size());
    return false;
  }

  fKeySet = true;
  return true;
}

bool CCrypter::SetKey(const CKeyingMaterial &chNewKey, const std::vector<uint8_t> &chNewIV) {
  if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_IV_SIZE) return false;

  memcpy(vchKey.data(), chNewKey.data(), chNewKey.size());
  memcpy(vchIV.data(), chNewIV.data(), chNewIV.size());

  fKeySet = true;
  return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial &vchPlaintext, std::vector<uint8_t> &vchCiphertext) const {
  if (!fKeySet) return false;

  // max ciphertext len for a n bytes of plaintext is
  // n + AES_BLOCKSIZE bytes
  vchCiphertext.resize(vchPlaintext.size() + AES_BLOCKSIZE);

  AES256CBCEncrypt enc(vchKey.data(), vchIV.data(), true);
  size_t nLen = enc.Encrypt(&vchPlaintext[0], vchPlaintext.size(), &vchCiphertext[0]);
  if (nLen < vchPlaintext.size()) return false;
  vchCiphertext.resize(nLen);

  return true;
}

bool CCrypter::Decrypt(const std::vector<uint8_t> &vchCiphertext, CKeyingMaterial &vchPlaintext) const {
  if (!fKeySet) return false;

  // plaintext will always be equal to or lesser than length of ciphertext
  int nLen = vchCiphertext.size();

  vchPlaintext.resize(nLen);

  AES256CBCDecrypt dec(vchKey.data(), vchIV.data(), true);
  nLen = dec.Decrypt(&vchCiphertext[0], vchCiphertext.size(), &vchPlaintext[0]);
  if (nLen == 0) return false;
  vchPlaintext.resize(nLen);
  return true;
}

static bool EncryptSecret(const CKeyingMaterial &vInMasterKey, const CKeyingMaterial &vchPlaintext, const uint256 &nIV,
                          std::vector<uint8_t> &vchCiphertext) {
  CCrypter cKeyCrypter;
  std::vector<uint8_t> chIV(WALLET_CRYPTO_IV_SIZE);
  memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
  if (!cKeyCrypter.SetKey(vInMasterKey, chIV)) return false;
  return cKeyCrypter.Encrypt(*((const CKeyingMaterial *)&vchPlaintext), vchCiphertext);
}

bool DecryptSecret(const CKeyingMaterial &vInMasterKey, const std::vector<uint8_t> &vchCiphertext, const uint256 &nIV,
                   CKeyingMaterial &vchPlaintext) {
  CCrypter cKeyCrypter;
  std::vector<uint8_t> chIV(WALLET_CRYPTO_IV_SIZE);
  memcpy(&chIV[0], &nIV, WALLET_CRYPTO_IV_SIZE);
  if (!cKeyCrypter.SetKey(vInMasterKey, chIV)) return false;
  return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial *)&vchPlaintext));
}

static bool DecryptKey(const CKeyingMaterial &vInMasterKey, const std::vector<uint8_t> &vchCryptedSecret,
                       const CPubKey &vchPubKey, CKey &key) {
  CKeyingMaterial vchSecret;
  if (!DecryptSecret(vInMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret)) return false;

  if (vchSecret.size() != 32) return false;

  key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
  return key.VerifyPubKey(vchPubKey);
}

bool CCryptoKeyStore::Lock() {
  {
    LOCK(cs_KeyStore);
    vMasterKey.clear();
    pwalletMain->zwalletMain->Lock();
  }

  NotifyStatusChanged(this);
  return true;
}
void CCryptoKeyStore::SetMaster(const CKeyingMaterial &vInMasterKey) {
  {
    LOCK(cs_KeyStore);
    vMasterKey = vInMasterKey;
  }
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial &vInMasterKey) {
  {
    LOCK(cs_KeyStore);

    bool keyPass = false;
    bool keyFail = false;
    for (auto &mi : mapCryptedKeys) {
      const CPubKey &vchPubKey = mi.second.first;
      const std::vector<uint8_t> &vchCryptedSecret = mi.second.second;
      CKey key;
      if (!DecryptKey(vInMasterKey, vchCryptedSecret, vchPubKey, key)) {
        keyFail = true;
        break;
      }
      keyPass = true;
      if (fDecryptionThoroughlyChecked) break;
    }
    if (keyPass && keyFail) {
      LogPrintf(
          "The wallet is probably corrupted: Some keys decrypt but "
          "not all.\n");
      assert(false);
    }
    if (keyFail || !keyPass) return false;
    vMasterKey = vInMasterKey;
    fDecryptionThoroughlyChecked = true;

    uint256 hashSeed;
    if (gWalletDB.ReadCurrentSeedHash(hashSeed)) {
      uint256 nSeed;
      if (!GetDeterministicSeed(hashSeed, nSeed)) {
        return error("Failed to read ZKP seed from DB. Wallet is probably corrupt.");
      }
      pwalletMain->zwalletMain->SetMasterSeed(nSeed, false);
    } else {
      // First time this wallet has been unlocked with dZkp. Get HD MasterKey for ZKP
      uint256 seed = pwalletMain->GetHDMasterKeySeed();
      // LogPrintf("%s: first run of zkp wallet detected, new seed generated. Seedhash=%s\n",
      // __func__,Hash(seed.begin(), seed.end()).GetHex());
      pwalletMain->zwalletMain->SetMasterSeed(seed, true);
      pwalletMain->zwalletMain->GenerateZMintPool();
    }
  }
  NotifyStatusChanged(this);
  return true;
}

bool CCryptoKeyStore::AddKeyPubKey(const CKey &key, const CPubKey &pubkey) {
  {
    LOCK(cs_KeyStore);
    if (IsLocked()) return false;

    std::vector<uint8_t> vchCryptedSecret;
    CKeyingMaterial vchSecret(key.begin(), key.end());
    if (!EncryptSecret(vMasterKey, vchSecret, pubkey.GetHash(), vchCryptedSecret)) return false;
    if (!AddCryptedKey(pubkey, vchCryptedSecret)) return false;
  }
  return true;
}

bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<uint8_t> &vchCryptedSecret) {
  {
    LOCK(cs_KeyStore);
    mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
  }
  return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey &keyOut) const {
  {
    LOCK(cs_KeyStore);
    const auto mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
      const CPubKey &vchPubKey = (*mi).second.first;
      const std::vector<uint8_t> &vchCryptedSecret = (*mi).second.second;
      return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
    }
  }
  return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const {
  {
    LOCK(cs_KeyStore);
    auto mi = mapCryptedKeys.find(address);
    if (mi != mapCryptedKeys.end()) {
      vchPubKeyOut = (*mi).second.first;
      return true;
    }
    // Check for watch-only pubkeys
    return CBasicKeyStore::GetPubKey(address, vchPubKeyOut);
  }
  return false;
}

bool CCryptoKeyStore::AddDeterministicSeed(const uint256 &seed) {
  string strErr;
  uint256 hashSeed = Hash(seed.begin(), seed.end());
  if (!IsLocked()) {  // if we have password
    CKeyingMaterial kmSeed(seed.begin(), seed.end());
    vector<uint8_t> vchSeedSecret;
    // attempt encrypt
    if (EncryptSecret(vMasterKey, kmSeed, hashSeed, vchSeedSecret)) {
      // write to wallet with hashSeed as unique key
      if (gWalletDB.WriteZKPSeed(hashSeed, vchSeedSecret)) { return true; }
    }
    strErr = "encrypt seed";
  }
  strErr = "save since wallet is locked";
  // the use case for this is no password set seed, mint dZkp,
  return error("s%: Failed to %s\n", __func__, strErr);
}

bool CCryptoKeyStore::GetDeterministicSeed(const uint256 &hashSeed, uint256 &seedOut) {
  string strErr;
  if (!IsLocked()) {  // if we have password
    vector<uint8_t> vchCryptedSeed;
    // read encrypted seed
    if (gWalletDB.ReadZKPSeed(hashSeed, vchCryptedSeed)) {
      uint256 seedRetrieved = uint256S(ReverseEndianString(HexStr(vchCryptedSeed)));
      // this checks if the hash of the seed we just read matches the hash given, meaning it is not encrypted
      // the use case for this is when not crypted, seed is set, then password set, the seed not yet crypted in memory
      if (hashSeed == Hash(seedRetrieved.begin(), seedRetrieved.end())) {
        seedOut = seedRetrieved;
        return true;
      }

      CKeyingMaterial kmSeed;
      // attempt decrypt
      if (DecryptSecret(vMasterKey, vchCryptedSeed, hashSeed, kmSeed)) {
        seedOut = uint256S(ReverseEndianString(HexStr(kmSeed)));
        return true;
      }
      strErr = "decrypt seed";
    } else {
      strErr = "read seed from wallet";
    }
  } else {
    strErr = "read seed; wallet is locked";
  }
  return error("%s: Failed to %s\n", __func__, strErr);
  //    return error("Failed to decrypt deterministic seed %s", IsLocked() ? "Wallet is locked!" : "");
}

struct CCryptoKeyStoreSignalSigs {
  boost::signals2::signal<CCryptoKeyStore::NotifyStatusChangedSig> NotifyStatusChanged;
} g_crypter_signals;

#define ADD_SIGNALS_IMPL_WRAPPER(signal_name)                                                              \
  boost::signals2::connection CCryptoKeyStore::signal_name##_connect(std::function<signal_name##Sig> fn) { \
    return g_crypter_signals.signal_name.connect(fn);                                                      \
  }                                                                                                        \
  void CCryptoKeyStore::signal_name##_disconnect(std::function<signal_name##Sig> fn) {                     \
    return g_crypter_signals.signal_name.disconnect(&fn);                                                  \
  }

ADD_SIGNALS_IMPL_WRAPPER(NotifyStatusChanged)

void CCryptoKeyStore::NotifyStatusChanged(CCryptoKeyStore *wallet) { g_crypter_signals.NotifyStatusChanged(wallet); }
