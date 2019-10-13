// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <cassert>

using namespace std;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime,
                                 uint32_t nNonce, uint32_t nBits, int32_t nHeaderVersion,
                                 const CAmount& genesisReward) {
  CMutableTransaction txNew;
  txNew.nTransactionVersion = 1;
  txNew.vin.resize(1);
  txNew.vout.resize(1);
  txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4)
                                     << std::vector<uint8_t>((const uint8_t*)pszTimestamp,
                                                             (const uint8_t*)pszTimestamp + strlen(pszTimestamp));
  txNew.vout[0].nValue = genesisReward;
  txNew.vout[0].scriptPubKey = genesisOutputScript;

  CBlock genesis;
  genesis.nTime = nTime;
  genesis.nBits = nBits;
  genesis.nNonce = nNonce;
  genesis.nHeaderVersion = nHeaderVersion;
  genesis.vtx.push_back(txNew);
  genesis.hashPrevBlock.SetNull();
  genesis.hashMerkleRoot = genesis.BuildMerkleTree();
  return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation transaction
 * cannot be spent since it did not originally exist in the database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000,
 * hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893,
 * vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase
 * 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nHeaderVersion,
                                 const CAmount& genesisReward) {
  const char* pszTimestamp = "The Big Hack: How China Used a Tiny Chip to Infiltrate U.S. Companies, Oct 4, 2018";
  const CScript genesisOutputScript = CScript()
                                      << ParseHex(
                                             "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3"
                                             "f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f")

                                      << OP_CHECKSIG;
  return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nHeaderVersion, genesisReward);
}

/**
 * Main network
 */

//   What makes a good checkpoint block?
// + Is surrounded by blocks with reasonable timestamps
//   (no blocks before with a timestamp after, none after with
//    timestamp before)
// + Contains no strange transactions
static Checkpoints::MapCheckpoints mapCheckpoints = {
    {259201, uint256S("1c9121bf9329a6234bfd1ea2d91515f19cd96990725265253f4b164283ade5dd")}};
static const Checkpoints::CCheckpointData dataMain = {
    &mapCheckpoints,
    1525106065,  // * UNIX timestamp of last checkpoint block
    2498834,     // * total number of transactions between genesis and last checkpoint
                 //   (the tx=... number in the SetBestChain debug.log lines)
    2000         // * estimated number of transactions per day after checkpoint
};

static Checkpoints::MapCheckpoints mapCheckpointsTestnet = {{0, uint256S("0x001")}};
static const Checkpoints::CCheckpointData dataTestnet = {&mapCheckpointsTestnet, 1740710, 0, 250};
static Checkpoints::MapCheckpoints mapCheckpointsRegtest = {{0, uint256S("0x001")}};
static const Checkpoints::CCheckpointData dataRegtest = {&mapCheckpointsRegtest, 1454124731, 0, 100};

class CMainParams : public CChainParams {
 public:
  CMainParams() {
    networkID = CBaseChainParams::MAIN;
    strNetworkID = "main";
    /**
     * The message start string is designed to be unlikely to occur in normal data.
     * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
     * a large 4-byte int at any alignment.
     */
    pchMessageStart[0] = 0x90;
    pchMessageStart[1] = 0xc4;
    pchMessageStart[2] = 0xfd;
    pchMessageStart[3] = 0xe9;
    nDefaultPort = 44444;
    bnProofOfWorkLimit = ~arith_uint256(0) >> 20;  // Tessa starting difficulty is 1 / 2^12
    nSubsidyHalvingInterval = 210000;
    nMaxReorganizationDepth = 100;
    nEnforceBlockUpgradeMajority = 750;
    nRejectBlockOutdatedMajority = 950;
    nToCheckBlockUpgradeMajority = 1000;
    nMinerThreads = 0;
    nTargetTimespan = 1 * 60;  // Tessa: 1 day
    nTargetSpacing = 1 * 60;   // Tessa: 1 minute
    nMaturity = 100;

    /** Height or Time Based Activations **/
    nLastPOWBlock = 259200;
    /**
     * Build the genesis block. Note that the output of the genesis coinbase cannot
     * be spent as it did not originally exist in the database.
     *
     * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618,
     * nBits=1e0ffff0, nNonce=28917698, vtx=1) CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
     *     CTxIn(COutPoint(000000, -1), coinbase
     * 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
     *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
     *   vMerkleTree: e0028e
     */
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vout.resize(1);

    genesis = CreateGenesisBlock(1538753921, 1026102636, 0x1e0ffff0, 1, 5 * COIN);
    hashGenesisBlock = genesis.GetHash();
    std::cout << "main Genesis block = " << hashGenesisBlock.ToString() << "\n";
    std::cout << "Genesis MerkleRoot = " << genesis.hashMerkleRoot.ToString() << "\n";
    assert(hashGenesisBlock == uint256S("4ae60fe937feeb705bcb3d631f16d1fcc103744335336ca3d2ee19e4ffc935fd"));
    assert(genesis.hashMerkleRoot == uint256S("73f27d6a3e0291af32c45da791d04edefa6b7b3dff9943146eedc9e4150e4650"));

    // vSeeds.push_back(CDNSSeedData("fuzzbawls.pw", "tessa.seed.fuzzbawls.pw"));     // Primary DNS Seeder from
    // Fuzzbawls

    bch32_hrp = "ct";
    bch32_sec = "st";

    fMiningRequiresPeers = true;
    fAllowMinDifficultyBlocks = false;
    fDefaultConsistencyChecks = false;
    fRequireStandard = true;
    fMineBlocksOnDemand = false;
    fSkipProofOfWorkCheck = false;
    fTestnetToBeDeprecatedFieldRPC = false;
    fHeadersFirstSyncingActive = false;

    nPoolMaxTransactions = 3;
    nStakeMinAge = 60 * 60;  // 60 minutes
  }

  const Checkpoints::CCheckpointData& Checkpoints() const { return dataMain; }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams {
 public:
  CTestNetParams() {
    networkID = CBaseChainParams::TESTNET;
    strNetworkID = "test";
    pchMessageStart[0] = 0x45;
    pchMessageStart[1] = 0x76;
    pchMessageStart[2] = 0x65;
    pchMessageStart[3] = 0xba;
    nDefaultPort = 44446;
    nEnforceBlockUpgradeMajority = 51;
    nRejectBlockOutdatedMajority = 75;
    nToCheckBlockUpgradeMajority = 100;
    nMinerThreads = 0;
    nTargetTimespan = 1 * 60;  // Tessa: 1 day
    nTargetSpacing = 20;
    nLastPOWBlock = 200;
    nMaturity = 15;
    nModifierUpdateBlock = 51197;  // approx Mon, 17 Apr 2017 04:00:00 GMT
    //! Modify the testnet genesis block so the timestamp is valid for a later start.
    genesis = CreateGenesisBlock(1538753921, 1026102636, 0x1e0ffff0, 1, 5 * COIN);
    hashGenesisBlock = genesis.GetHash();
    // std::cout << hashGenesisBlock.ToString() << "\n";
    // std::cout << genesis.hashMerkleRoot.ToString();

    // assert(hashGenesisBlock == uint256("0xece31863b3e3f325b96a4fcf8a78fdc0a5dc67abb4c73aa56188b42faa7148bb"));

    vFixedSeeds.clear();
    vSeeds.clear();
    // vSeeds.push_back(CDNSSeedData("fuzzbawls.pw", "tessa-testnet.seed.fuzzbawls.pw"));

    bch32_hrp = "tt";
    bch32_sec = "ts";

    fAllowMinDifficultyBlocks = true;
    fDefaultConsistencyChecks = false;
    fRequireStandard = true;
    fMineBlocksOnDemand = false;
    fTestnetToBeDeprecatedFieldRPC = true;

    fSkipProofOfWorkCheck = true;
    fMiningRequiresPeers = false;
    bnProofOfWorkLimit = ~arith_uint256(0) >> 1;

    nStakeMinAge = 1 * 60;   // 1 minute for Testnet
    nModifierInterval = 60;  // ? why this value
    nStakeTargetSpacing = 60;

    nPoolMaxTransactions = 2;
  }
  const Checkpoints::CCheckpointData& Checkpoints() const { return dataTestnet; }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams {
 public:
  CRegTestParams() {
    networkID = CBaseChainParams::REGTEST;
    strNetworkID = "regtest";
    strNetworkID = "regtest";
    pchMessageStart[0] = 0xa1;
    pchMessageStart[1] = 0xcf;
    pchMessageStart[2] = 0x7e;
    pchMessageStart[3] = 0xac;
    nSubsidyHalvingInterval = 150;
    nEnforceBlockUpgradeMajority = 750;
    nRejectBlockOutdatedMajority = 950;
    nToCheckBlockUpgradeMajority = 1000;
    nMinerThreads = 1;
    nTargetTimespan = 24 * 60 * 60;  // Tessa: 1 day
    nTargetSpacing = 10;         // Tessa: 10 seconds
    bnProofOfWorkLimit = ~arith_uint256(0) >> 1;

    genesis = CreateGenesisBlock(1538753921, 1026102636, 0x1e0ffff0, 1, 5 * COIN);
    hashGenesisBlock = genesis.GetHash();
    nDefaultPort = 44448;
    std::cout << hashGenesisBlock.ToString() << "\n";
    // assert(hashGenesisBlock == uint256("0x57939ce0a96bf42965fee5956528a456d0edfb879b8bd699bcbb4786d27b979d"));

    bch32_hrp = "xx";  //??
    bch32_sec = "ss";  //??

    vFixedSeeds.clear();  //! Testnet mode doesn't have any fixed seeds.
    vSeeds.clear();       //! Testnet mode doesn't have any DNS seeds.

    fMiningRequiresPeers = false;
    fAllowMinDifficultyBlocks = true;
    fDefaultConsistencyChecks = true;
    fRequireStandard = false;
    fMineBlocksOnDemand = true;
    fTestnetToBeDeprecatedFieldRPC = false;
  }
  const Checkpoints::CCheckpointData& Checkpoints() const { return dataRegtest; }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams {
 public:
  CUnitTestParams() {
    networkID = CBaseChainParams::UNITTEST;
    strNetworkID = "unittest";
    nDefaultPort = 44450;
    vFixedSeeds.clear();  //! Unit test mode doesn't have any fixed seeds.
    vSeeds.clear();       //! Unit test mode doesn't have any DNS seeds.

    fMiningRequiresPeers = false;
    fDefaultConsistencyChecks = true;
    fAllowMinDifficultyBlocks = false;
    fMineBlocksOnDemand = true;
  }

  const Checkpoints::CCheckpointData& Checkpoints() const {
    // UnitTest share the same checkpoints as MAIN
    return dataMain;
  }

  //! Published setters to allow changing values in unit test cases
  virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval) {
    nSubsidyHalvingInterval = anSubsidyHalvingInterval;
  }
  virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority) {
    nEnforceBlockUpgradeMajority = anEnforceBlockUpgradeMajority;
  }
  virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority) {
    nRejectBlockOutdatedMajority = anRejectBlockOutdatedMajority;
  }
  virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority) {
    nToCheckBlockUpgradeMajority = anToCheckBlockUpgradeMajority;
  }
  virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks) {
    fDefaultConsistencyChecks = afDefaultConsistencyChecks;
  }
  virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) {
    fAllowMinDifficultyBlocks = afAllowMinDifficultyBlocks;
  }
  virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;

static CChainParams* pCurrentParams = nullptr;

CModifiableParams* ModifiableParams() {
  assert(pCurrentParams);
  assert(pCurrentParams == &unitTestParams);
  return (CModifiableParams*)&unitTestParams;
}

const CChainParams& Params() {
  assert(pCurrentParams);
  return *pCurrentParams;
}

CChainParams& Params(CBaseChainParams::Network network) {
  switch (network) {
    case CBaseChainParams::MAIN:
      return mainParams;
    case CBaseChainParams::TESTNET:
      return testNetParams;
    case CBaseChainParams::REGTEST:
      return regTestParams;
    case CBaseChainParams::UNITTEST:
      return unitTestParams;
    default:
      assert(false && "Unimplemented network");
      return mainParams;
  }
}

void SelectParams(CBaseChainParams::Network network) {
  SelectBaseParams(network);
  pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine() {
  CBaseChainParams::Network network = NetworkIdFromCommandLine();
  if (network == CBaseChainParams::MAX_NETWORK_TYPES) return false;

  SelectParams(network);
  return true;
}
