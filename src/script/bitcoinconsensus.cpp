// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bitcoinconsensus.h"
#include "ecdsa/ecdsa.h"

#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "version.h"

namespace {

/** A class that deserializes a single CTransaction one time. */
class TxInputStream {
 public:
  TxInputStream(int nTypeIn, int nVersionIn, const uint8_t* txTo, size_t txToLen)
      : m_data(txTo), m_remaining(txToLen) {}

  TxInputStream& read(char* pch, size_t nSize) {
    if (nSize > m_remaining) throw std::ios_base::failure(std::string(__func__) + ": end of data");

    if (pch == nullptr) throw std::ios_base::failure(std::string(__func__) + ": bad destination buffer");

    if (m_data == nullptr) throw std::ios_base::failure(std::string(__func__) + ": bad source buffer");

    memcpy(pch, m_data, nSize);
    m_remaining -= nSize;
    m_data += nSize;
    return *this;
  }

  template <typename T> TxInputStream& operator>>(T& obj) {
    ::Unserialize(*this, obj);
    return *this;
  }

 private:
  const uint8_t* m_data;
  size_t m_remaining;
};

inline int set_error(bitcoinconsensus_error* ret, bitcoinconsensus_error serror) {
  if (ret) *ret = serror;
  return 0;
}

struct ECCryptoClosure {
  ECCVerifyHandle handle;
};

ECCryptoClosure instance_of_eccryptoclosure;

}  // namespace

int bitcoinconsensus_verify_script(const uint8_t* scriptPubKey, uint32_t scriptPubKeyLen, const uint8_t* txTo,
                                   uint32_t txToLen, unsigned int nIn, unsigned int flags,
                                   bitcoinconsensus_error* err) {
  try {
    TxInputStream stream(SER_NETWORK, PROTOCOL_VERSION, txTo, txToLen);
    CTransaction tx;
    stream >> tx;
    if (nIn >= tx.vin.size()) return set_error(err, bitcoinconsensus_ERR_TX_INDEX);
    if (tx.GetSerializeSize() != txToLen) return set_error(err, bitcoinconsensus_ERR_TX_SIZE_MISMATCH);

    // Regardless of the verification result, the tx did not error.
    set_error(err, bitcoinconsensus_ERR_OK);

    return VerifyScript(tx.vin[nIn].scriptSig, CScript(scriptPubKey, scriptPubKey + scriptPubKeyLen), flags,
                        TransactionSignatureChecker(&tx, nIn), nullptr);
  } catch (const std::exception&) {
    return set_error(err, bitcoinconsensus_ERR_TX_DESERIALIZE);  // Error deserializing
  }
}

uint32_t bitcoinconsensus_version() {
  // Just use the API version for now
  return BITCOINCONSENSUS_API_VER;
}
