// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEYSTORE_H
#define BITCOIN_KEYSTORE_H

#include "support/allocators/secure.h"
#include "sync.h"
#include <cstdint>
#include <set>
#include <vector>

namespace ecdsa {
  class CPubKey;
  class CKey;
  class CKeyID;
}

class CScript;
class CScriptID;

/** A virtual base class for key stores */
class CKeyStore {
 protected:
  mutable CCriticalSection cs_KeyStore;

 public:
  virtual ~CKeyStore() {}

  //! Add a key to the store.
  virtual bool AddKeyPubKey(const ecdsa::CKey& key, const ecdsa::CPubKey& pubkey) = 0;
  virtual bool AddKey(const ecdsa::CKey& key);

  //! Check whether a key corresponding to a given address is present in the store.
  virtual bool HaveKey(const ecdsa::CKeyID& address) const = 0;
  virtual bool GetKey(const ecdsa::CKeyID& address, ecdsa::CKey& keyOut) const = 0;
  virtual void GetKeys(std::set<ecdsa::CKeyID>& setAddress) const = 0;
  virtual bool GetPubKey(const ecdsa::CKeyID& address, ecdsa::CPubKey& vchPubKeyOut) const;

  //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
  virtual bool AddCScript(const CScript& redeemScript) = 0;
  virtual bool HaveCScript(const CScriptID& hash) const = 0;
  virtual bool GetCScript(const CScriptID& hash, CScript& redeemScriptOut) const = 0;

  //! Support for Watch-only addresses
  virtual bool AddWatchOnly(const CScript& dest) = 0;
  virtual bool RemoveWatchOnly(const CScript& dest) = 0;
  virtual bool HaveWatchOnly(const CScript& dest) const = 0;
  virtual bool HaveWatchOnly() const = 0;

  //! Support for MultiSig addresses
  virtual bool AddMultiSig(const CScript& dest) = 0;
  virtual bool RemoveMultiSig(const CScript& dest) = 0;
  virtual bool HaveMultiSig(const CScript& dest) const = 0;
  virtual bool HaveMultiSig() const = 0;
};

using KeyMap = std::map<ecdsa::CKeyID, ecdsa::CKey>;
using ScriptMap =  std::map<CScriptID, CScript>;
using WatchOnlySet =  std::set<CScript>;
using MultiSigScriptSet =  std::set<CScript>;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore {
 protected:
  KeyMap mapKeys;
  ScriptMap mapScripts;
  WatchOnlySet setWatchOnly;
  MultiSigScriptSet setMultiSig;

 public:
  bool AddKeyPubKey(const ecdsa::CKey& key, const ecdsa::CPubKey& pubkey);
  bool HaveKey(const ecdsa::CKeyID& address) const;
  void GetKeys(std::set<ecdsa::CKeyID>& setAddress) const;
  bool GetKey(const ecdsa::CKeyID& address, ecdsa::CKey& keyOut) const;

  virtual bool AddCScript(const CScript& redeemScript);
  virtual bool HaveCScript(const CScriptID& hash) const;
  virtual bool GetCScript(const CScriptID& hash, CScript& redeemScriptOut) const;

  virtual bool AddWatchOnly(const CScript& dest);
  virtual bool RemoveWatchOnly(const CScript& dest);
  virtual bool HaveWatchOnly(const CScript& dest) const;
  virtual bool HaveWatchOnly() const;

  virtual bool AddMultiSig(const CScript& dest);
  virtual bool RemoveMultiSig(const CScript& dest);
  virtual bool HaveMultiSig(const CScript& dest) const;
  virtual bool HaveMultiSig() const;
};

using CKeyingMaterial= std::vector<uint8_t, secure_allocator<uint8_t> >;
using CryptedKeyMap =  std::map<ecdsa::CKeyID, std::pair<ecdsa::CPubKey, std::vector<uint8_t> > >;

#endif  // BITCOIN_KEYSTORE_H
