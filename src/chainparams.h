// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// clang-format off
#pragma once

#include "chainparamsbase.h"
#include "checkpoints.h"
#include "primitives/block.h"
#include "protocol.h"
#include "uint256.h"
#include "arith_uint256.h"
#include <vector>

typedef uint8_t MessageStartChars[MESSAGE_START_SIZE];

struct CDNSSeedData {
  std::string name, host;
  CDNSSeedData(const std::string& strName, const std::string& strHost) : name(strName), host(strHost) {}
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Tessa system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum BaseType {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,     // BIP16
        EXT_PUBLIC_KEY, // BIP32
        EXT_SECRET_KEY, // BIP32
        EXT_COIN_TYPE,  // BIP44
        MAX_BASE58_TYPES
    };

    const std::string& Bch32HRP() const { return bch32_hrp; }
    const uint256& HashGenesisBlock() const { return hashGenesisBlock; }
    const MessageStartChars& MessageStart() const { return pchMessageStart; }
    int GetDefaultPort() const { return nDefaultPort; }
    const arith_uint256& ProofOfWorkLimit() const { return bnProofOfWorkLimit; }
    int SubsidyHalvingInterval() const { return nSubsidyHalvingInterval; }
    /** Used to check majorities for block version upgrade */
    int EnforceBlockUpgradeMajority() const { return nEnforceBlockUpgradeMajority; }
    int RejectBlockOutdatedMajority() const { return nRejectBlockOutdatedMajority; }
    int ToCheckBlockUpgradeMajority() const { return nToCheckBlockUpgradeMajority; }
    int MaxReorganizationDepth() const { return nMaxReorganizationDepth; }

    /** Used if GenerateBitcoins is called with a negative number of threads */
    int DefaultMinerThreads() const { return nMinerThreads; }
    const CBlock& GenesisBlock() const { return genesis; }
    /** Make miner wait to have peers to avoid wasting work */
    bool MiningRequiresPeers() const { return fMiningRequiresPeers; }
    /** Headers first syncing is disabled */
    bool HeadersFirstSyncingActive() const { return fHeadersFirstSyncingActive; };
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Allow mining of a min-difficulty block */
    bool AllowMinDifficultyBlocks() const { return fAllowMinDifficultyBlocks; }
    /** Skip proof-of-work check: allow mining of any difficulty block */
    bool SkipProofOfWorkCheck() const { return fSkipProofOfWorkCheck; }
    /** Make standard checks */
    bool RequireStandard() const { return fRequireStandard; }
    int64_t TargetTimespan() const { return nTargetTimespan; }
    int64_t TargetSpacing() const { return nTargetSpacing; }
    int64_t Interval() const { return nTargetTimespan / nTargetSpacing; }
    int COINBASE_MATURITY() const { return nMaturity; }
    CAmount MaxMoneyOut() const { return nMaxMoneyOut; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** In the future use NetworkIDString() for RPC fields */
    bool TestnetToBeDeprecatedFieldRPC() const { return fTestnetToBeDeprecatedFieldRPC; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }
    const std::vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    const std::vector<uint8_t>& Prefix(BaseType type) const { return Prefixes[type]; }
    const std::vector<CAddress>& FixedSeeds() const { return vFixedSeeds; }
    virtual const Checkpoints::CCheckpointData& Checkpoints() const = 0;
    int PoolMaxTransactions() const { return nPoolMaxTransactions; }

    CBaseChainParams::Network NetworkID() const { return networkID; }

    /** Zerocoin **/
    int Zerocoin_MaxSpendsPerTransaction() const { return nMaxZerocoinSpendsPerTransaction; }
    CAmount Zerocoin_MintFee() const { return nMinZerocoinMintFee; }
    int Zerocoin_MintRequiredConfirmations() const { return nMintRequiredConfirmations; }
    int Zerocoin_RequiredAccumulation() const { return nRequiredAccumulation; }
    int Zerocoin_DefaultSpendSecurity() const { return nDefaultSecurityLevel; }
    int Zerocoin_HeaderVersion() const { return nZerocoinHeaderVersion; }

    /** Height or Time Based Activations **/
    int LAST_POW_BLOCK() const { return nLastPOWBlock; }
    int Zerocoin_StartHeight() const { return nZerocoinStartHeight; }
    int StakeMinAge() const { return nStakeMinAge; }
    int ModifierInterval() const { return nModifierInterval; }
    int StakeTargetSpacing() const { return nStakeTargetSpacing; }
    
protected:
    CChainParams() {}

    uint256 hashGenesisBlock;
    MessageStartChars pchMessageStart;
    int nDefaultPort;
    arith_uint256 bnProofOfWorkLimit;
    int nMaxReorganizationDepth;
    int nSubsidyHalvingInterval;
    int nEnforceBlockUpgradeMajority;
    int nRejectBlockOutdatedMajority;
    int nToCheckBlockUpgradeMajority;
    int64_t nTargetTimespan;
    int64_t nTargetSpacing;
    int nLastPOWBlock;
    int nMaturity;
    CAmount nMaxMoneyOut;
    int nMinerThreads;
    std::vector<CDNSSeedData> vSeeds;
    std::string bch32_hrp;
    std::vector<uint8_t> Prefixes[MAX_BASE58_TYPES];
    CBaseChainParams::Network networkID;
    std::string strNetworkID;
    CBlock genesis;
    std::vector<CAddress> vFixedSeeds;
    bool fMiningRequiresPeers;
    bool fAllowMinDifficultyBlocks;
    bool fDefaultConsistencyChecks;
    bool fRequireStandard;
    bool fMineBlocksOnDemand;
    bool fSkipProofOfWorkCheck;
    bool fTestnetToBeDeprecatedFieldRPC;
    bool fHeadersFirstSyncingActive;
    int nPoolMaxTransactions;
    std::string zerocoinModulus;
    int nMaxZerocoinSpendsPerTransaction;
    CAmount nMinZerocoinMintFee;
    int nMintRequiredConfirmations;
    int nRequiredAccumulation;
    int nDefaultSecurityLevel;
    int nZerocoinHeaderVersion;
    int nZerocoinStartHeight;
    int nStakeMinAge;
    int nStakeTargetSpacing;
    int nModifierUpdateBlock;
    int nModifierInterval;
 };

/**
 * Modifiable parameters interface is used by test cases to adapt the parameters in order
 * to test specific features more easily. Test cases should always restore the previous
 * values after finalization.
 */

class CModifiableParams {
 public:
  //! Published setters to allow changing values in unit test cases
  virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval) = 0;
  virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority) = 0;
  virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority) = 0;
  virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority) = 0;
  virtual void setDefaultConsistencyChecks(bool aDefaultConsistencyChecks) = 0;
  virtual void setAllowMinDifficultyBlocks(bool aAllowMinDifficultyBlocks) = 0;
  virtual void setSkipProofOfWorkCheck(bool aSkipProofOfWorkCheck) = 0;
};

/**
 * Return the currently selected parameters. This won't change after app startup
 * outside of the unit tests.
 */
const CChainParams& Params();

/** Return parameters for the given network. */
CChainParams& Params(CBaseChainParams::Network network);

/** Get modifiable network parameters (UNITTEST only) */
CModifiableParams* ModifiableParams();

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

// clang-format on
