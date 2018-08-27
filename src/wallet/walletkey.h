// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey {
 public:
  CPrivKey vchPrivKey;
  int64_t nTimeCreated;
  int64_t nTimeExpires;
  std::string strComment;
  //! todo: add something to note what created it (user, getnewaddress, change)
  //!   maybe should have a map<string, string> property map

  CWalletKey(int64_t nExpires = 0);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    int nType = s.GetType();
    int nVersion = s.GetVersion();
    if (!(nType & SER_GETHASH)) READWRITE(nVersion);
    READWRITE(vchPrivKey);
    READWRITE(nTimeCreated);
    READWRITE(nTimeExpires);
    READWRITE(LIMITED_STRING(strComment, 65536));
  }
};
