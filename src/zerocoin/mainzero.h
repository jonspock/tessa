// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once

// Forward Declarations
namespace libzerocoin {
class PublicCoin;
class CoinSpend;
}  // namespace libzerocoin
class CValidationState;
class uint256;
class CTxOut;
class CTransaction;
class CBlockIndex;
class CBigNum;

bool CheckZerocoinMint(const uint256& txHash, const CTxOut& txout, CValidationState& state, bool fCheckOnly);
bool ContextualCheckZerocoinMint(const CTransaction& tx, const libzerocoin::PublicCoin& coin,
                                 const CBlockIndex* pindex);
bool ContextualCheckZerocoinSpend(const CTransaction& tx, const libzerocoin::CoinSpend& spend, CBlockIndex* pindex,
                                  const uint256& hashBlock);
bool CheckZerocoinSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state);
bool ValidatePublicCoin(const CBigNum& value);
