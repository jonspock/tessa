// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "libzerocoin/Params.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

using namespace std;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime,
                                 uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward) {
  CMutableTransaction txNew;
  txNew.nVersion = 1;
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
  genesis.nVersion = nVersion;
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
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion,
                                 const CAmount& genesisReward) {
  const char* pszTimestamp = "February 5, 2014: The Black Hills are not for sale - 1868 Is The LAW!";
  const CScript genesisOutputScript = CScript()
                                      << ParseHex(
                                             "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3"
                                             "f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f")

                                      << OP_CHECKSIG;
  return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
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
    {259201, uint256("1c9121bf9329a6234bfd1ea2d91515f19cd96990725265253f4b164283ade5dd")},
    {908000, uint256("202708f8c289b676fceb832a079ff6b308a28608339acbf7584de533619d014d")},
    {1142400, uint256("98aff9d605bf123247f98b1e3a02567eb5799d208d78ec30fb89737b1c1f79c5")}};
static const Checkpoints::CCheckpointData data = {
    &mapCheckpoints,
    1525106065,  // * UNIX timestamp of last checkpoint block
    2498834,     // * total number of transactions between genesis and last checkpoint
                 //   (the tx=... number in the SetBestChain debug.log lines)
    2000         // * estimated number of transactions per day after checkpoint
};

static Checkpoints::MapCheckpoints mapCheckpointsTestnet = {{0, uint256("0x001")}};

static const Checkpoints::CCheckpointData dataTestnet = {&mapCheckpointsTestnet, 1740710, 0, 250};

static Checkpoints::MapCheckpoints mapCheckpointsRegtest = {{0, uint256("0x001")}};

static const Checkpoints::CCheckpointData dataRegtest = {&mapCheckpointsRegtest, 1454124731, 0, 100};

libzerocoin::ZerocoinParams* CChainParams::Zerocoin_Params() const {
  assert(this);
  static CBigNum bnDecModulus = 0;
  if (!bnDecModulus) bnDecModulus.SetDec(zerocoinModulus);
  static libzerocoin::ZerocoinParams ZCParamsDec = libzerocoin::ZerocoinParams(bnDecModulus);

  return &ZCParamsDec;
}

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
    bnProofOfWorkLimit = ~uint256(0) >> 20;  // Club starting difficulty is 1 / 2^12
    nSubsidyHalvingInterval = 210000;
    nMaxReorganizationDepth = 100;
    nEnforceBlockUpgradeMajority = 750;
    nRejectBlockOutdatedMajority = 950;
    nToCheckBlockUpgradeMajority = 1000;
    nMinerThreads = 0;
    nTargetTimespan = 1 * 60;  // Club: 1 day
    nTargetSpacing = 1 * 60;   // Club: 1 minute
    nMaturity = 100;
    nMaxMoneyOut = 21000000 * COIN;

    /** Height or Time Based Activations **/
    nLastPOWBlock = 259200;
    nModifierUpdateBlock = 615800;
    nZerocoinStartHeight = 230;
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

    genesis = CreateGenesisBlock(1390747675, 2091390249, 0x1e0ffff0, 1, 5000 * COIN);
    hashGenesisBlock = genesis.GetHash();
    std::cout << "main = " << hashGenesisBlock.ToString() << "\n";
    std::cout << genesis.hashMerkleRoot.ToString() << "\n";
    assert(hashGenesisBlock == uint256("0x00000c7c73d8ce604178dae13f0fc6ec0be3275614366d44b1b4b5c6e238c60c"));
    assert(genesis.hashMerkleRoot == uint256("0x62d496378e5834989dd9594cfc168dbb76f84a39bbda18286cddc7d1d1589f4f"));

    // vSeeds.push_back(CDNSSeedData("fuzzbawls.pw", "club.seed.fuzzbawls.pw"));     // Primary DNS Seeder from
    // Fuzzbawls

    base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 28);  // Start with "C"
    base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 7);
    base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 212);
    base58Prefixes[EXT_PUBLIC_KEY] = {0x02, 0x2D, 0x25, 0x33};
    base58Prefixes[EXT_SECRET_KEY] = {0x02, 0x21, 0x31, 0x2B};
    // 	BIP44 coin type is from https://github.com/satoshilabs/slips/blob/master/slip-0044.md
    base58Prefixes[EXT_COIN_TYPE] = {0x80, 0x00, 0x00, 0x77};

    fMiningRequiresPeers = true;
    fAllowMinDifficultyBlocks = false;
    fDefaultConsistencyChecks = false;
    fRequireStandard = true;
    fMineBlocksOnDemand = false;
    fSkipProofOfWorkCheck = false;
    fTestnetToBeDeprecatedFieldRPC = false;
    fHeadersFirstSyncingActive = false;

    nPoolMaxTransactions = 3;
    strSporkKey =
        "04B433E6598390C992F4F022F20D3B4CBBE691652EE7C48243B81701CBDB7CC7D7BF0EE09E154E6FCBF2043D65AF4E9E97B89B5DBAF830"
        "D83B9B7F469A6C45A717";

    /** Zerocoin */
        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
            "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
            "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
            "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
            "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
            "31438167899885040445364023527381951378636564391212010397122822120720357";
    nMaxZerocoinSpendsPerTransaction = 7;  // Assume about 20kb each
    nMinZerocoinMintFee = 1 * CENT;        // high fee required for zerocoin mints
    nMintRequiredConfirmations = 20;       // the maximum amount of confirmations until accumulated in 19
    nRequiredAccumulation = 1;
    nDefaultSecurityLevel = 100;  // full security level for accumulators
    nZerocoinHeaderVersion = 1;   // Block headers must be this version once zerocoin is active
  }

  const Checkpoints::CCheckpointData& Checkpoints() const { return data; }
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
    nTargetTimespan = 1 * 60;  // Club: 1 day
    nTargetSpacing = 20;
    nLastPOWBlock = 200;
    nMaturity = 15;
    nModifierUpdateBlock = 51197;  // approx Mon, 17 Apr 2017 04:00:00 GMT
    nMaxMoneyOut = 43199500 * COIN;
    nZerocoinStartHeight = 230;
    //! Modify the testnet genesis block so the timestamp is valid for a later start.
    genesis = CreateGenesisBlock(1411587941, 2091634749, 0x1e0ffff0, 1, 5000 * COIN);
    hashGenesisBlock = genesis.GetHash();
    // std::cout << hashGenesisBlock.ToString() << "\n";
    // std::cout << genesis.hashMerkleRoot.ToString();

    // assert(hashGenesisBlock == uint256("0xece31863b3e3f325b96a4fcf8a78fdc0a5dc67abb4c73aa56188b42faa7148bb"));

    vFixedSeeds.clear();
    vSeeds.clear();
    // vSeeds.push_back(CDNSSeedData("fuzzbawls.pw", "club-testnet.seed.fuzzbawls.pw"));

    base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 87);  // Testnet club addresses start with 'c'
    base58Prefixes[SCRIPT_ADDRESS] =
        std::vector<unsigned char>(1, 9);  // Testnet club script addresses start with '4' or '5'
    base58Prefixes[SECRET_KEY] =
        std::vector<unsigned char>(1, 239);  // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
    // Testnet club BIP32 pubkeys start with 'DRKV'
    base58Prefixes[EXT_PUBLIC_KEY] = {0x3a, 0x80, 0x61, 0xa0};
    // Testnet club BIP32 prvkeys start with 'DRKP'
    base58Prefixes[EXT_SECRET_KEY] = {0x3a, 0x80, 0x58, 0x37};
    // Testnet club BIP44 coin type is '1' (All coin's testnet default)
    base58Prefixes[EXT_COIN_TYPE] = {0x80, 0x00, 0x00, 0x01};

    fAllowMinDifficultyBlocks = true;
    fDefaultConsistencyChecks = false;
    fRequireStandard = true;
    fMineBlocksOnDemand = false;
    fTestnetToBeDeprecatedFieldRPC = true;

    fSkipProofOfWorkCheck = true;
    fMiningRequiresPeers = false;
    bnProofOfWorkLimit = ~uint256(0) >> 1;

    nPoolMaxTransactions = 2;
    strSporkKey =
        "04348C2F50F90267E64FACC65BFDC9D0EB147D090872FB97ABAE92E9A36E6CA60983E28E741F8E7277B11A7479B626AC115BA31463AC48"
        "178A5075C5A9319D4A38";
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
    nTargetTimespan = 24 * 60 * 60;  // Club: 1 day
    nTargetSpacing = 1 * 60;         // Club: 1 minutes
    bnProofOfWorkLimit = ~uint256(0) >> 1;

    genesis = CreateGenesisBlock(1390748221, 4, 0x207fffff, 1, 5000 * COIN);
    hashGenesisBlock = genesis.GetHash();
    nDefaultPort = 44448;
    std::cout << hashGenesisBlock.ToString() << "\n";
    assert(hashGenesisBlock == uint256("0x57939ce0a96bf42965fee5956528a456d0edfb879b8bd699bcbb4786d27b979d"));

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
    return data;
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

static CChainParams* pCurrentParams = 0;

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
