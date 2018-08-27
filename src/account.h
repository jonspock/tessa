// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "streams.h"
#include "wallet/wallet_functions.h"

/**
 * Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount {
 public:
  CPubKey vchPubKey;

  CAccount() { SetNull(); }

  void SetNull() { vchPubKey = CPubKey(); }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    int nType = s.GetType();
    int nVersion = s.GetVersion();
    if (!(nType & SER_GETHASH)) READWRITE(nVersion);
    READWRITE(vchPubKey);
  }
};

/**
 * Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry {
 public:
  std::string strAccount;
  CAmount nCreditDebit;
  int64_t nTime;
  std::string strOtherAccount;
  std::string strComment;
  mapValue_t mapValue;
  int64_t nOrderPos;  //! position in ordered transaction list
  uint64_t nEntryNo;

  CAccountingEntry() { SetNull(); }

  void SetNull() {
    nCreditDebit = 0;
    nTime = 0;
    strAccount.clear();
    strOtherAccount.clear();
    strComment.clear();
    nOrderPos = -1;
    nEntryNo = 0;
  }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    int nType = s.GetType();
    int nVersion = s.GetVersion();
    if (!(nType & SER_GETHASH)) READWRITE(nVersion);
    //! Note: strAccount is serialized as part of the key, not here.
    READWRITE(nCreditDebit);
    READWRITE(nTime);
    READWRITE(LIMITED_STRING(strOtherAccount, 65536));

    if (!ser_action.ForRead()) {
      WriteOrderPos(nOrderPos, mapValue);

      if (!(mapValue.empty() && _ssExtra.empty())) {
        CDataStream ss(nType, nVersion);
        ss.insert(ss.begin(), '\0');
        ss << mapValue;
        ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
        strComment.append(ss.str());
      }
    }

    READWRITE(LIMITED_STRING(strComment, 65536));

    size_t nSepPos = strComment.find("\0", 0, 1);
    if (ser_action.ForRead()) {
      mapValue.clear();
      if (std::string::npos != nSepPos) {
        CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType, nVersion);
        ss >> mapValue;
        _ssExtra = std::vector<char>(ss.begin(), ss.end());
      }
      ReadOrderPos(nOrderPos, mapValue);
    }
    if (std::string::npos != nSepPos) strComment.erase(nSepPos);

    mapValue.erase("n");
  }

 private:
  std::vector<char> _ssExtra;
};
