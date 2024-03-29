// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include "amount.h"
#include "script/script.h"
#include "serialize.h"
#include "uint256.h"

#include <list>

class CTransaction;

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint {
 public:
  uint256 hash;
  uint32_t n;

  COutPoint() { SetNull(); }
  COutPoint(uint256 hashIn, uint32_t nIn) {
    hash = hashIn;
    n = nIn;
  }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(FLATDATA(*this));
  }

  void SetNull() {
    hash.SetNull();
    n = (uint32_t)-1;
  }
  bool IsNull() const { return (hash.IsNull() && n == (uint32_t)-1); }
  bool IsMasternodeReward(const CTransaction* tx) const;

  friend bool operator<(const COutPoint& a, const COutPoint& b) {
    return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
  }

  friend bool operator==(const COutPoint& a, const COutPoint& b) { return (a.hash == b.hash && a.n == b.n); }

  friend bool operator!=(const COutPoint& a, const COutPoint& b) { return !(a == b); }

  std::string ToString() const;
  std::string ToStringShort() const;

  uint256 GetHash();
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn {
 public:
  COutPoint prevout;
  CScript scriptSig;
  uint32_t nSequence;
  CScript prevPubKey;

  CTxIn() { nSequence = std::numeric_limits<uint32_t>::max(); }

  explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn = CScript(),
                 uint32_t nSequenceIn = std::numeric_limits<uint32_t>::max());
  CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn = CScript(),
        uint32_t nSequenceIn = std::numeric_limits<uint32_t>::max());

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(prevout);
    READWRITE(scriptSig);
    READWRITE(nSequence);
  }

  bool IsFinal() const { return (nSequence == std::numeric_limits<uint32_t>::max()); }

  friend bool operator==(const CTxIn& a, const CTxIn& b) {
    return (a.prevout == b.prevout && a.scriptSig == b.scriptSig && a.nSequence == b.nSequence);
  }

  friend bool operator!=(const CTxIn& a, const CTxIn& b) { return !(a == b); }

  std::string ToString() const;
};

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut {
 public:
  CAmount nValue;
  CScript scriptPubKey;
  int nRounds;

  CTxOut() { SetNull(); }

  CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(nValue);
    READWRITE(scriptPubKey);
  }

  void SetNull() {
    nValue = -1;
    scriptPubKey.clear();
    nRounds = -10;  // an initial value, should be no way to get this by calculations
  }

  bool IsNull() const { return (nValue == -1); }

  void SetEmpty() {
    nValue = 0;
    scriptPubKey.clear();
  }

  bool IsEmpty() const { return (nValue == 0 && scriptPubKey.empty()); }

  uint256 GetHash() const;

  bool IsDust(const int& minRelayTxFee) const {
    // "Dust" is defined in terms of CTransaction::minRelayTxFee, which has units upiv-per-kilobyte.
    // If you'd pay more than 1/3 in fees to spend something, then we consider it dust.
    // A typical txout is 34 bytes big, and will need a CTxIn of at least 148 bytes to spend
    // i.e. total is 148 + 32 = 182 bytes. Default -minrelaytxfee is 10000 upiv per kB
    // and that means that fee per txout is 182 * 10000 / 1000 = 1820 upiv.
    // So dust is a txout less than 1820 *3 = 5460 upiv
    // with default -minrelaytxfee = minRelayTxFee = 10000 upiv per kB.
    return (nValue < 3 * minRelayTxFee);
  }

  bool IsZerocoinMint() const { return !scriptPubKey.empty() && scriptPubKey.IsZerocoinMint(); }

  friend bool operator==(const CTxOut& a, const CTxOut& b) {
    return (a.nValue == b.nValue && a.scriptPubKey == b.scriptPubKey && a.nRounds == b.nRounds);
  }

  friend bool operator!=(const CTxOut& a, const CTxOut& b) { return !(a == b); }

  std::string ToString() const;
};

struct CMutableTransaction;

/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction {
 private:
  /** Memory only. */
  const uint256 hash;
  void UpdateHash() const;

 public:
  static const int32_t CURRENT_VERSION = 1;

  // The local variables are made const to prevent unintended modification
  // without updating the cached hash value. However, CTransaction is not
  // actually immutable; deserialization and assignment are implemented,
  // and bypass the constness. This is safe, as they update the entire
  // structure, including the hash.
  const int32_t nTransactionVersion;
  std::vector<CTxIn> vin;
  std::vector<CTxOut> vout;
  const uint32_t nLockTime;
  // const uint32_t nTime;

  /** Construct a CTransaction that qualifies as IsNull() */
  CTransaction();

  /** Convert a CMutableTransaction into a CTransaction. */
  CTransaction(const CMutableTransaction& tx);

  CTransaction& operator=(const CTransaction& tx);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(*const_cast<int32_t*>(&this->nTransactionVersion));
    // Modify code below if changes needed for nTransactionVersion
    READWRITE(*const_cast<std::vector<CTxIn>*>(&vin));
    READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
    READWRITE(*const_cast<uint32_t*>(&nLockTime));
    if (ser_action.ForRead()) UpdateHash();
  }

  bool IsNull() const { return vin.empty() && vout.empty(); }

  const uint256& GetHash() const { return hash; }

  // Return sum of txouts.
  CAmount GetValueOut() const;
  // GetValueIn() is a method on CCoinsViewCache, because
  // inputs must be known to compute value in.

  // Compute priority, given priority of inputs and (optionally) tx size
  double ComputePriority(double dPriorityInputs, uint32_t nTxSize = 0) const;

  // Compute modified tx size for priority calculation (optionally given tx size)
  uint32_t CalculateModifiedSize(unsigned int nTxSize = 0) const;

  bool IsZerocoinSpend() const {
    return (vin.size() > 0 && (vin[0].prevout.hash).IsNull() && vin[0].scriptSig[0] == OP_ZEROCOINSPEND);
  }

  bool IsZerocoinMint() const {
    for (const CTxOut& txout : vout) {
      if (txout.scriptPubKey.IsZerocoinMint()) return true;
    }
    return false;
  }

  bool ContainsZerocoins() const {
    bool yeah = IsZerocoinSpend() || IsZerocoinMint();
    return yeah;
  }

  CAmount GetZerocoinMinted() const;
  CAmount GetZerocoinSpent() const;
  int GetZerocoinMintCount() const;

  bool UsesUTXO(const COutPoint out);
  std::list<COutPoint> GetOutPoints() const;

  bool IsCoinBase() const { return (vin.size() == 1 && vin[0].prevout.IsNull() && !ContainsZerocoins()); }

  bool IsCoinStake() const;

  friend bool operator==(const CTransaction& a, const CTransaction& b) { return a.hash == b.hash; }

  friend bool operator!=(const CTransaction& a, const CTransaction& b) { return a.hash != b.hash; }

  std::string ToString() const;

 };

/** A mutable version of CTransaction. */
struct CMutableTransaction {
  int32_t nTransactionVersion;
  std::vector<CTxIn> vin;
  std::vector<CTxOut> vout;
  uint32_t nLockTime;

  CMutableTransaction();
  CMutableTransaction(const CTransaction& tx);

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(nTransactionVersion);
    //nVersion = nTransactionVersion;
    READWRITE(vin);
    READWRITE(vout);
    READWRITE(nLockTime);
  }

  /** Compute the hash of this CMutableTransaction. This is computed on the
   * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
   */
  uint256 GetHash() const;

  std::string ToString() const;

  friend bool operator==(const CMutableTransaction& a, const CMutableTransaction& b) {
    return a.GetHash() == b.GetHash();
  }

  friend bool operator!=(const CMutableTransaction& a, const CMutableTransaction& b) { return !(a == b); }
};

#endif  // BITCOIN_PRIMITIVES_TRANSACTION_H
