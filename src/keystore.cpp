// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "keystore.h"
#include "ecdsa/key.h"
#include "script/script.h"
#include "script/standard.h"
#include "util.h"
#include "wallet/crypter.h"

using namespace ecdsa;

bool CKeyStore::GetPubKey(const CKeyID& address, CPubKey& vchPubKeyOut) const {
  CKey key;
  if (!GetKey(address, key)) return false;
  vchPubKeyOut = key.GetPubKey();
  return true;
}

bool CKeyStore::AddKey(const CKey& key) { return AddKeyPubKey(key, key.GetPubKey()); }

bool CBasicKeyStore::AddCScript(const CScript& redeemScript) {
  if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    return error("CBasicKeyStore::AddCScript() : redeemScripts > %i bytes are invalid", MAX_SCRIPT_ELEMENT_SIZE);

  LOCK(cs_KeyStore);
  mapScripts[CScriptID(redeemScript)] = redeemScript;
  return true;
}

bool CBasicKeyStore::HaveCScript(const CScriptID& hash) const {
  LOCK(cs_KeyStore);
  return mapScripts.count(hash) > 0;
}

bool CBasicKeyStore::GetCScript(const CScriptID& hash, CScript& redeemScriptOut) const {
  LOCK(cs_KeyStore);
  auto mi = mapScripts.find(hash);
  if (mi != mapScripts.end()) {
    redeemScriptOut = (*mi).second;
    return true;
  }
  return false;
}

bool CBasicKeyStore::AddWatchOnly(const CScript& dest) {
  LOCK(cs_KeyStore);
  setWatchOnly.insert(dest);
  return true;
}

bool CBasicKeyStore::RemoveWatchOnly(const CScript& dest) {
  LOCK(cs_KeyStore);
  setWatchOnly.erase(dest);
  return true;
}

bool CBasicKeyStore::HaveWatchOnly(const CScript& dest) const {
  LOCK(cs_KeyStore);
  return setWatchOnly.count(dest) > 0;
}

bool CBasicKeyStore::HaveWatchOnly() const {
  LOCK(cs_KeyStore);
  return (!setWatchOnly.empty());
}

bool CBasicKeyStore::AddMultiSig(const CScript& dest) {
  LOCK(cs_KeyStore);
  setMultiSig.insert(dest);
  return true;
}

bool CBasicKeyStore::RemoveMultiSig(const CScript& dest) {
  LOCK(cs_KeyStore);
  setMultiSig.erase(dest);
  return true;
}

bool CBasicKeyStore::HaveMultiSig(const CScript& dest) const {
  LOCK(cs_KeyStore);
  return setMultiSig.count(dest) > 0;
}

bool CBasicKeyStore::HaveMultiSig() const {
  LOCK(cs_KeyStore);
  return (!setMultiSig.empty());
}

// Should not be used
bool CBasicKeyStore::HaveKey(const CKeyID& address) const { return false; }
bool CBasicKeyStore::GetKey(const ecdsa::CKeyID& address, ecdsa::CKey& keyOut) const { return false; }
void CBasicKeyStore::GetKeys(std::set<CKeyID>& setAddress) const {}
bool CBasicKeyStore::AddKeyPubKey(const CKey& key, const CPubKey& pubkey) { return false; }
