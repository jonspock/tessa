#pragma once
#include "pubkey.h"
#include "serialize.h"

/** A key pool entry */
class CKeyPool {
 public:
  int64_t nTime;
  CPubKey vchPubKey;

  CKeyPool();
  CKeyPool(const CPubKey& vchPubKeyIn);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    int nTyp = s.GetType();
    int nVersion = s.GetVersion();
    if (!(nTyp & SER_GETHASH)) READWRITE(nVersion);
    READWRITE(nTime);
    READWRITE(vchPubKey);
  }
};
