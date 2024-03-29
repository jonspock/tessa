// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SERIALIZE_H
#define BITCOIN_SERIALIZE_H

#include "libzerocoin/Denominations.h"
#include "libzerocoin/SpendType.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class CScript;

static const uint32_t MAX_SIZE = 0x02000000;

/**
 * Used to bypass the rule against non-const reference to temporary
 * where it makes sense with wrappers such as CFlatData or CTxDB
 */
template <typename T> inline T& REF(const T& val) { return const_cast<T&>(val); }

/**
 * Used to acquire a non-const pointer "this" to generate bodies
 * of const serialization operations from a template
 */
template <typename T> inline T* NCONST_PTR(const T* val) { return const_cast<T*>(val); }

/**
 * Get begin pointer of vector (non-const version).
 * @note These functions avoid the undefined case of indexing into an empty
 * vector, as well as that of indexing after the end of the vector.
 */
template <class T, class TAl> inline T* begin_ptr(std::vector<T, TAl>& v) { return v.empty() ? nullptr : &v[0]; }
/** Get begin pointer of vector (const version) */
template <class T, class TAl> inline const T* begin_ptr(const std::vector<T, TAl>& v) {
  return v.empty() ? nullptr : &v[0];
}
/** Get end pointer of vector (non-const version) */
template <class T, class TAl> inline T* end_ptr(std::vector<T, TAl>& v) {
  return v.empty() ? nullptr : (&v[0] + v.size());
}
/** Get end pointer of vector (const version) */
template <class T, class TAl> inline const T* end_ptr(const std::vector<T, TAl>& v) {
  return v.empty() ? nullptr : (&v[0] + v.size());
}

/////////////////////////////////////////////////////////////////
//
// Templates for serializing to anything that looks like a stream,
// i.e. anything that supports .read(char*, size_t) and .write(char*, size_t)
//

enum {
  // primary actions
  SER_NETWORK = (1 << 0),
  SER_DISK = (1 << 1),
  SER_GETHASH = (1 << 2),
};

#define READWRITE(obj) (::SerReadWrite(s, (obj), ser_action))

/**
 * Implement three methods for serializable objects. These are actually wrappers over
 * "SerializationOp" template, which implements the body of each class' serialization
 * code. Adding "ADD_SERIALIZE_METHODS" in the body of the class causes these wrappers to be
 * added as members.
 */
#define ADD_SERIALIZE_METHODS                                    \
  size_t GetSerializeSize(void) const {                          \
    CSizeComputer s;                                             \
    NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize()); \
    return s.size();                                             \
  }                                                              \
  template <typename Stream> void Serialize(Stream& s) const {   \
    NCONST_PTR(this)->SerializationOp(s, CSerActionSerialize()); \
  }                                                              \
  template <typename Stream> void Unserialize(Stream& s) { SerializationOp(s, CSerActionUnserialize()); }

/*
 * Basic Types
 */
#define WRITEDATA(s, obj) s.write((char*)&(obj), sizeof(obj))
#define READDATA(s, obj) s.read((char*)&(obj), sizeof(obj))

inline uint32_t GetSerializeSize(char a) { return sizeof(a); }
inline uint32_t GetSerializeSize(unsigned char a) { return sizeof(a); }

inline uint32_t GetSerializeSize(uint16_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(uint32_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(uint64_t a) { return sizeof(a); }

// Signed types
inline uint32_t GetSerializeSize(int8_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(int16_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(int32_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(int64_t a) { return sizeof(a); }
inline uint32_t GetSerializeSize(float a) { return sizeof(a); }
inline uint32_t GetSerializeSize(double a) { return sizeof(a); }

template <typename Stream> inline void Serialize(Stream& s, char a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, int8_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, unsigned char a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, int16_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, uint16_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, int32_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, uint32_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, int64_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, uint64_t a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, float a) { WRITEDATA(s, a); }
template <typename Stream> inline void Serialize(Stream& s, double a) { WRITEDATA(s, a); }

template <typename Stream> inline void Unserialize(Stream& s, char& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, int8_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, unsigned char& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, int16_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, uint16_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, int32_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, uint32_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, int64_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, uint64_t& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, float& a) { READDATA(s, a); }
template <typename Stream> inline void Unserialize(Stream& s, double& a) { READDATA(s, a); }

inline uint32_t GetSerializeSize(bool a) { return sizeof(char); }
template <typename Stream> inline void Serialize(Stream& s, bool a) {
  char f = a;
  WRITEDATA(s, f);
}

template <typename Stream> inline void Unserialize(Stream& s, bool& a) {
  char f;
  READDATA(s, f);
  a = f;
}
// Serializatin for libzerocoin::CoinDenomination
inline uint32_t GetSerializeSize(libzerocoin::CoinDenomination a) { return sizeof(libzerocoin::CoinDenomination); }
template <typename Stream> inline void Serialize(Stream& s, libzerocoin::CoinDenomination a) {
  int f = libzerocoin::ZerocoinDenominationToInt(a);
  WRITEDATA(s, f);
}

template <typename Stream> inline void Unserialize(Stream& s, libzerocoin::CoinDenomination& a) {
  int f = 0;
  READDATA(s, f);
  a = libzerocoin::IntToZerocoinDenomination(f);
}

// Serialization for libzerocoin::SpendType
inline uint32_t GetSerializedSize(libzerocoin::SpendType a) { return sizeof(libzerocoin::SpendType); }
template <typename Stream> inline void Serialize(Stream& s, libzerocoin::SpendType a) {
  auto f = static_cast<uint8_t>(a);
  WRITEDATA(s, f);
}

template <typename Stream> inline void Unserialize(Stream& s, libzerocoin::SpendType& a) {
  uint8_t f = 0;
  READDATA(s, f);
  a = static_cast<libzerocoin::SpendType>(f);
}

/**
 * Compact Size
 * size <  253        -- 1 byte
 * size <= USHRT_MAX  -- 3 bytes  (253 + 2 bytes)
 * size <= UINT_MAX   -- 5 bytes  (254 + 4 bytes)
 * size >  UINT_MAX   -- 9 bytes  (255 + 8 bytes)
 */
inline uint32_t GetSizeOfCompactSize(uint64_t nSize) {
  if (nSize < 253)
    return sizeof(unsigned char);
  else if (nSize <= std::numeric_limits<uint16_t>::max())
    return sizeof(unsigned char) + sizeof(uint16_t);
  else if (nSize <= std::numeric_limits<uint32_t>::max())
    return sizeof(unsigned char) + sizeof(uint32_t);
  else
    return sizeof(unsigned char) + sizeof(uint64_t);
}

template <typename Stream> void WriteCompactSize(Stream& os, uint64_t nSize) {
  if (nSize < 253) {
    unsigned char chSize = nSize;
    WRITEDATA(os, chSize);
  } else if (nSize <= std::numeric_limits<uint16_t>::max()) {
    unsigned char chSize = 253;
    uint16_t xSize = nSize;
    WRITEDATA(os, chSize);
    WRITEDATA(os, xSize);
  } else if (nSize <= std::numeric_limits<uint32_t>::max()) {
    unsigned char chSize = 254;
    uint32_t xSize = nSize;
    WRITEDATA(os, chSize);
    WRITEDATA(os, xSize);
  } else {
    unsigned char chSize = 255;
    uint64_t xSize = nSize;
    WRITEDATA(os, chSize);
    WRITEDATA(os, xSize);
  }
  return;
}

template <typename Stream> uint64_t ReadCompactSize(Stream& is) {
  unsigned char chSize;
  READDATA(is, chSize);
  uint64_t nSizeRet = 0;
  if (chSize < 253) {
    nSizeRet = chSize;
  } else if (chSize == 253) {
    uint16_t xSize;
    READDATA(is, xSize);
    nSizeRet = xSize;
    if (nSizeRet < 253) throw std::ios_base::failure("non-canonical ReadCompactSize()");
  } else if (chSize == 254) {
    uint32_t xSize;
    READDATA(is, xSize);
    nSizeRet = xSize;
    if (nSizeRet < 0x10000u) throw std::ios_base::failure("non-canonical ReadCompactSize()");
  } else {
    uint64_t xSize;
    READDATA(is, xSize);
    nSizeRet = xSize;
    if (nSizeRet < 0x100000000ULL) throw std::ios_base::failure("non-canonical ReadCompactSize()");
  }
  if (nSizeRet > (uint64_t)MAX_SIZE) throw std::ios_base::failure("ReadCompactSize() : size too large");
  return nSizeRet;
}

/**
 * Variable-length integers: bytes are a MSB base-128 encoding of the number.
 * The high bit in each byte signifies whether another digit follows. To make
 * sure the encoding is one-to-one, one is subtracted from all but the last digit.
 * Thus, the byte sequence a[] with length len, where all but the last byte
 * has bit 128 set, encodes the number:
 *
 *  (a[len-1] & 0x7F) + sum(i=1..len-1, 128^i*((a[len-i-1] & 0x7F)+1))
 *
 * Properties:
 * * Very small (0-127: 1 byte, 128-16511: 2 bytes, 16512-2113663: 3 bytes)
 * * Every integer has exactly one encoding
 * * Encoding does not depend on size of original integer type
 * * No redundancy: every (infinite) byte sequence corresponds to a list
 *   of encoded integers.
 *
 * 0:         [0x00]  256:        [0x81 0x00]
 * 1:         [0x01]  16383:      [0xFE 0x7F]
 * 127:       [0x7F]  16384:      [0xFF 0x00]
 * 128:  [0x80 0x00]  16511: [0x80 0xFF 0x7F]
 * 255:  [0x80 0x7F]  65535: [0x82 0xFD 0x7F]
 * 2^32:           [0x8E 0xFE 0xFE 0xFF 0x00]
 */

//
// Can check if VarInt Type is signed or unsigned;
// Currently used for Signed although it shouldn't be
//
template <typename I> struct CheckVarIntType {
  constexpr CheckVarIntType() { static_assert(std::is_unsigned<I>::value, "Unsigned type required for CVarInt."); }
};

template <typename I> inline uint32_t GetSizeOfVarInt(I n) {
  int nRet = 0;
  while (true) {
    nRet++;
    if (n <= 0x7F) break;
    n = (n >> 7) - 1;
  }
  return nRet;
}

template <typename Stream, typename I> void WriteVarInt(Stream& os, I n) {
  unsigned char tmp[(sizeof(n) * 8 + 6) / 7];
  int len = 0;
  while (true) {
    tmp[len] = (n & 0x7F) | (len ? 0x80 : 0x00);
    if (n <= 0x7F) break;
    n = (n >> 7) - 1;
    len++;
  }
  do { WRITEDATA(os, tmp[len]); } while (len--);
}

template <typename Stream, typename I> I ReadVarInt(Stream& is) {
  I n = 0;
  while (true) {
    unsigned char chData;
    READDATA(is, chData);
    n = (n << 7) | (chData & 0x7F);
    if (chData & 0x80)
      n++;
    else
      return n;
  }
}

#define FLATDATA(obj) REF(CFlatData((char*)&(obj), (char*)&(obj) + sizeof(obj)))
#define VARINT(obj) REF(WrapVarInt(REF(obj)))
#define LIMITED_STRING(obj, n) REF(LimitedString<n>(REF(obj)))

/**
 * Wrapper for serializing arrays and POD.
 */
class CFlatData {
 protected:
  char* pbegin;
  char* pend;

 public:
  CFlatData(void* pbeginIn, void* pendIn) : pbegin((char*)pbeginIn), pend((char*)pendIn) {}
  template <class T, class TAl> explicit CFlatData(std::vector<T, TAl>& v) {
    pbegin = (char*)begin_ptr(v);
    pend = (char*)end_ptr(v);
  }
  char* begin() { return pbegin; }
  const char* begin() const { return pbegin; }
  char* end() { return pend; }
  const char* end() const { return pend; }

  uint32_t GetSerializeSize() const { return pend - pbegin; }

  template <typename Stream> void Serialize(Stream& s) const { s.write(pbegin, pend - pbegin); }

  template <typename Stream> void Unserialize(Stream& s) { s.read(pbegin, pend - pbegin); }
};

template <typename I> class CVarInt {
 protected:
  I& n;

 public:
  CVarInt(I& nIn) : n(nIn) {}

  uint32_t GetSerializeSize() const { return GetSizeOfVarInt<I>(n); }

  template <typename Stream> void Serialize(Stream& s) const { WriteVarInt<Stream, I>(s, n); }

  template <typename Stream> void Unserialize(Stream& s) { n = ReadVarInt<Stream, I>(s); }
};

template <size_t Limit> class LimitedString {
 protected:
  std::string& string;

 public:
  LimitedString(std::string& string) : string(string) {}

  template <typename Stream> void Unserialize(Stream& s) {
    size_t size = ReadCompactSize(s);
    if (size > Limit) { throw std::ios_base::failure("String length limit exceeded"); }
    string.resize(size);
    if (size != 0) s.read((char*)&string[0], size);
  }

  template <typename Stream> void Serialize(Stream& s) const {
    WriteCompactSize(s, string.size());
    if (!string.empty()) s.write((char*)&string[0], string.size());
  }

  uint32_t GetSerializeSize() const { return GetSizeOfCompactSize(string.size()) + string.size(); }
};

template <typename I> CVarInt<I> WrapVarInt(I& n) { return CVarInt<I>(n); }

/**
 * Forward declarations
 */

/**
 *  string
 */
template <typename C> uint32_t GetSerializeSize(const std::basic_string<C>& str);
template <typename Stream, typename C> void Serialize(Stream& os, const std::basic_string<C>& str);
template <typename Stream, typename C> void Unserialize(Stream& is, std::basic_string<C>& str);

/**
 * vector
 * vectors of unsigned char are a special case and are intended to be serialized as a single opaque blob.
 */
template <typename T, typename A> uint32_t GetSerializeSize_impl(const std::vector<T, A>& v, const unsigned char&);
template <typename T, typename A, typename V> uint32_t GetSerializeSize_impl(const std::vector<T, A>& v, const V&);
template <typename T, typename A> inline uint32_t GetSerializeSize(const std::vector<T, A>& v);
template <typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const unsigned char&);
template <typename Stream, typename T, typename A, typename V>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const V&);
template <typename Stream, typename T, typename A> inline void Serialize(Stream& os, const std::vector<T, A>& v);
template <typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const unsigned char&);
template <typename Stream, typename T, typename A, typename V>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const V&);
template <typename Stream, typename T, typename A> inline void Unserialize(Stream& is, std::vector<T, A>& v);

/**
 * others derived from vector
 */
extern inline uint32_t GetSerializeSize(const CScript& v);
template <typename Stream> void Serialize(Stream& os, const CScript& v);
template <typename Stream> void Unserialize(Stream& is, CScript& v);

/**
 * pair
 */
template <typename K, typename T> uint32_t GetSerializeSize(const std::pair<K, T>& item);
template <typename Stream, typename K, typename T> void Serialize(Stream& os, const std::pair<K, T>& item);
template <typename Stream, typename K, typename T> void Unserialize(Stream& is, std::pair<K, T>& item);

/**
 * map
 */
template <typename K, typename T, typename Pred, typename A>
uint32_t GetSerializeSize(const std::map<K, T, Pred, A>& m);
template <typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::map<K, T, Pred, A>& m);
template <typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::map<K, T, Pred, A>& m);

/**
 * set
 */
template <typename K, typename Pred, typename A> uint32_t GetSerializeSize(const std::set<K, Pred, A>& m);
template <typename Stream, typename K, typename Pred, typename A>
void Serialize(Stream& os, const std::set<K, Pred, A>& m);
template <typename Stream, typename K, typename Pred, typename A> void Unserialize(Stream& is, std::set<K, Pred, A>& m);

/**
 * If none of the specialized versions above matched, default to calling member function.
 * "int nType" is changed to "long nType" to keep from getting an ambiguous overload error.
 * The compiler will only cast int to long if none of the other templates matched.
 * Thanks to Boost serialization for this idea.
 */
template <typename T> inline uint32_t GetSerializeSize(const T& a) { return a.GetSerializeSize(); }

template <typename Stream, typename T> inline void Serialize(Stream& os, const T& a) { a.Serialize(os); }

template <typename Stream, typename T> inline void Unserialize(Stream& is, T& a) { a.Unserialize(is); }

/**
 * string
 */
template <typename C> uint32_t GetSerializeSize(const std::basic_string<C>& str) {
  return GetSizeOfCompactSize(str.size()) + str.size() * sizeof(str[0]);
}

template <typename Stream, typename C> void Serialize(Stream& os, const std::basic_string<C>& str) {
  WriteCompactSize(os, str.size());
  if (!str.empty()) os.write((char*)&str[0], str.size() * sizeof(str[0]));
}

template <typename Stream, typename C> void Unserialize(Stream& is, std::basic_string<C>& str) {
  uint32_t nSize = ReadCompactSize(is);
  str.resize(nSize);
  if (nSize != 0) is.read((char*)&str[0], nSize * sizeof(str[0]));
}

/**
 * vector
 */
template <typename T, typename A> uint32_t GetSerializeSize_impl(const std::vector<T, A>& v, const unsigned char&) {
  return (GetSizeOfCompactSize(v.size()) + v.size() * sizeof(T));
}

template <typename T, typename A, typename V> uint32_t GetSerializeSize_impl(const std::vector<T, A>& v, const V&) {
  uint32_t nSize = GetSizeOfCompactSize(v.size());
  for (typename std::vector<T, A>::const_iterator vi = v.begin(); vi != v.end(); ++vi) nSize += GetSerializeSize((*vi));
  return nSize;
}

template <typename T, typename A> inline uint32_t GetSerializeSize(const std::vector<T, A>& v) {
  return GetSerializeSize_impl(v, T());
}

template <typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const unsigned char&) {
  WriteCompactSize(os, v.size());
  if (!v.empty()) os.write((char*)&v[0], v.size() * sizeof(T));
}

template <typename Stream, typename T, typename A, typename V>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const V&) {
  WriteCompactSize(os, v.size());
  for (const auto& vi : v) ::Serialize(os, vi);
}

template <typename Stream, typename T, typename A> inline void Serialize(Stream& os, const std::vector<T, A>& v) {
  Serialize_impl(os, v, T());
}

template <typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const unsigned char&) {
  // Limit size per read so bogus size value won't cause out of memory
  v.clear();
  uint32_t nSize = ReadCompactSize(is);
  uint32_t i = 0;
  while (i < nSize) {
    uint32_t blk = std::min(nSize - i, (uint32_t)(1 + 4999999 / sizeof(T)));
    v.resize(i + blk);
    is.read((char*)&v[i], blk * sizeof(T));
    i += blk;
  }
}

template <typename Stream, typename T, typename A, typename V>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const V&) {
  v.clear();
  uint32_t nSize = ReadCompactSize(is);
  uint32_t i = 0;
  uint32_t nMid = 0;
  while (nMid < nSize) {
    nMid += 5000000 / sizeof(T);
    if (nMid > nSize) nMid = nSize;
    v.resize(nMid);
    for (; i < nMid; i++) Unserialize(is, v[i]);
  }
}

template <typename Stream, typename T, typename A> inline void Unserialize(Stream& is, std::vector<T, A>& v) {
  Unserialize_impl(is, v, T());
}

/**
 * others derived from vector
 */
inline uint32_t GetSerializeSize(const CScript& v) { return GetSerializeSize((const std::vector<unsigned char>&)v); }

template <typename Stream> void Serialize(Stream& os, const CScript& v) {
  Serialize(os, (const std::vector<unsigned char>&)v);
}

template <typename Stream> void Unserialize(Stream& is, CScript& v) { Unserialize(is, (std::vector<unsigned char>&)v); }

/**
 * pair
 */
template <typename K, typename T> uint32_t GetSerializeSize(const std::pair<K, T>& item) {
  return GetSerializeSize(item.first) + GetSerializeSize(item.second);
}

template <typename Stream, typename K, typename T> void Serialize(Stream& os, const std::pair<K, T>& item) {
  Serialize(os, item.first);
  Serialize(os, item.second);
}

template <typename Stream, typename K, typename T> void Unserialize(Stream& is, std::pair<K, T>& item) {
  Unserialize(is, item.first);
  Unserialize(is, item.second);
}

/**
 * map
 */
template <typename K, typename T, typename Pred, typename A>
uint32_t GetSerializeSize(const std::map<K, T, Pred, A>& m) {
  uint32_t nSize = GetSizeOfCompactSize(m.size());
  for (const auto& mi : m) nSize += GetSerializeSize(mi);
  return nSize;
}

template <typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::map<K, T, Pred, A>& m) {
  WriteCompactSize(os, m.size());
  for (const auto& mi : m) Serialize(os, mi);
}

template <typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::map<K, T, Pred, A>& m) {
  m.clear();
  uint32_t nSize = ReadCompactSize(is);
  auto mi = m.begin();
  for (uint32_t i = 0; i < nSize; i++) {
    std::pair<K, T> item;
    Unserialize(is, item);
    mi = m.insert(mi, item);
  }
}

/**
 * set
 */
template <typename K, typename Pred, typename A> uint32_t GetSerializeSize(const std::set<K, Pred, A>& m) {
  uint32_t nSize = GetSizeOfCompactSize(m.size());
  for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
    nSize += GetSerializeSize((*it));
  return nSize;
}

template <typename Stream, typename K, typename Pred, typename A>
void Serialize(Stream& os, const std::set<K, Pred, A>& m) {
  WriteCompactSize(os, m.size());
  for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it) Serialize(os, (*it));
}

template <typename Stream, typename K, typename Pred, typename A>
void Unserialize(Stream& is, std::set<K, Pred, A>& m) {
  m.clear();
  uint32_t nSize = ReadCompactSize(is);
  typename std::set<K, Pred, A>::iterator it = m.begin();
  for (uint32_t i = 0; i < nSize; i++) {
    K key;
    Unserialize(is, key);
    it = m.insert(it, key);
  }
}

/**
 * Support for ADD_SERIALIZE_METHODS and READWRITE macro
 */
struct CSerActionSerialize {
  bool ForRead() const { return false; }
};
struct CSerActionUnserialize {
  bool ForRead() const { return true; }
};

template <typename Stream, typename T>
inline void SerReadWrite(Stream& s, const T& obj, CSerActionSerialize ser_action) {
  ::Serialize(s, obj);
}

template <typename Stream, typename T> inline void SerReadWrite(Stream& s, T& obj, CSerActionUnserialize ser_action) {
  ::Unserialize(s, obj);
}

class CSizeComputer {
 protected:
  size_t nSize = 0;

 public:
  int nType = 0;
  int nVersion = 0;

  CSizeComputer(int nTypeIn, int nVersionIn) {}
  CSizeComputer() = default;

  CSizeComputer& write(const char* psz, size_t nSize) {
    this->nSize += nSize;
    return *this;
  }

  /** Pretend _nSize bytes are written, without specifying them. */
  void seek(size_t _nSize) { this->nSize += _nSize; }

  template <typename T> CSizeComputer& operator<<(const T& obj) {
    ::Serialize(*this, obj);
    return (*this);
  }
  int GetVersion() const { return nVersion; }
  int GetType() const { return nType; }

  size_t size() const { return nSize; }
};

#endif  // BITCOIN_SERIALIZE_H
