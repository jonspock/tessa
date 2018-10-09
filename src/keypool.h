#pragma once

#include "serialize.h"

namespace bls {
class CPubKey;
}

/** A key pool entry */
class CKeyPool {
 public:
  int64_t nTime;
  bls::CPubKey vchPubKey;

  CKeyPool();
  CKeyPool(const bls::CPubKey& vchPubKeyIn);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    int nTyp = s.GetType();
    int nVersion = s.GetVersion();
    if (!(nTyp & SER_GETHASH)) READWRITE(nVersion);
    READWRITE(nTime);
    READWRITE(vchPubKey);
  }
};
