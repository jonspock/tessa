// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers 
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include "pubkey.h"
#include "privkey.h"

#include "base58.h"
#include "script/script.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

#include <string>
#include <vector>

using namespace std;
using namespace ecdsa;

static const string strSecret1     ("87vK7Vayi3QLsuiva5yWSuVwSMhMcRM9dBsaD6JXMD1P5vnjRFn");
static const string strSecret2     ("87FGYGFDg5SYfdD4XL593hr7do6f52czPecVsYSAXi8N4RGeS9i");
static const string strSecret1C    ("YRYJwfAyJ9c2jhi3T2xQyLijGvM7yLTw4izDaNQLxBzgUYrQiPmJ");
static const string strSecret2C    ("YNZyazHkwUbkmUpEYsBGWwHnHQTy2n9rJy1gS5k54YXVx3pE8n6N");
static const CBitcoinAddress addr1 ("DBFi8XAE1rcdCQfkv9w22n8Y9RxgaJnrDD");
static const CBitcoinAddress addr2 ("DPvKfv1FVp69yZMDzeuugvfZ9pzYiMv1bs");
static const CBitcoinAddress addr1C("DNPrHK9ezAAUVExFDpZ7EE1xWpPskgp1gP");
static const CBitcoinAddress addr2C("DNBVSAoc2whPFjZVAZ1pQbXPJk1LRrDC8Q");

static const string strAddressBad("Xta1praZQjyELweyMByXyiREw1ZRsjXzVP");


TEST_CASE("key_test1")
{
  
  SetupEnvironment();
  // Also Chainparams - Because UnitTest should work with BTC adddresses above!
  SelectParams(CBaseChainParams::UNITTEST);

  CBitcoinSecret bsecret1, bsecret2, bsecret1C, bsecret2C, baddress1;
    REQUIRE( bsecret1.SetString (strSecret1));
    REQUIRE( bsecret2.SetString (strSecret2));
    REQUIRE( bsecret1C.SetString(strSecret1C));
    REQUIRE( bsecret2C.SetString(strSecret2C));
    REQUIRE(!baddress1.SetString(strAddressBad));

    CKey key1  = bsecret1.GetKey();
    REQUIRE(key1.IsCompressed() == false);
    CKey key2  = bsecret2.GetKey();
    REQUIRE(key2.IsCompressed() == false);
    CKey key1C = bsecret1C.GetKey();
    REQUIRE(key1C.IsCompressed() == true);
    CKey key2C = bsecret2C.GetKey();
    REQUIRE(key2C.IsCompressed() == true);

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    REQUIRE(key1.VerifyPubKey(pubkey1));
    REQUIRE(!key1.VerifyPubKey(pubkey1C));
    REQUIRE(!key1.VerifyPubKey(pubkey2));
    REQUIRE(!key1.VerifyPubKey(pubkey2C));

    REQUIRE(!key1C.VerifyPubKey(pubkey1));
    REQUIRE(key1C.VerifyPubKey(pubkey1C));
    REQUIRE(!key1C.VerifyPubKey(pubkey2));
    REQUIRE(!key1C.VerifyPubKey(pubkey2C));

    REQUIRE(!key2.VerifyPubKey(pubkey1));
    REQUIRE(!key2.VerifyPubKey(pubkey1C));
    REQUIRE(key2.VerifyPubKey(pubkey2));
    REQUIRE(!key2.VerifyPubKey(pubkey2C));

    REQUIRE(!key2C.VerifyPubKey(pubkey1));
    REQUIRE(!key2C.VerifyPubKey(pubkey1C));
    REQUIRE(!key2C.VerifyPubKey(pubkey2));
    REQUIRE(key2C.VerifyPubKey(pubkey2C));

    //    REQUIRE(addr1.Get()  == CTxDestination(pubkey1.GetID()));
    //    REQUIRE(addr2.Get()  == CTxDestination(pubkey2.GetID()));
    //    REQUIRE(addr1C.Get() == CTxDestination(pubkey1C.GetID()));
    //    REQUIRE(addr2C.Get() == CTxDestination(pubkey2C.GetID()));

    for (int n=0; n<16; n++)
    {
        string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        vector<unsigned char> sign1, sign2, sign1C, sign2C;

        REQUIRE(key1.Sign (hashMsg, sign1));
        REQUIRE(key2.Sign (hashMsg, sign2));
        REQUIRE(key1C.Sign(hashMsg, sign1C));
        REQUIRE(key2C.Sign(hashMsg, sign2C));

        REQUIRE( pubkey1.Verify(hashMsg, sign1));
        REQUIRE(!pubkey1.Verify(hashMsg, sign2));
        REQUIRE( pubkey1.Verify(hashMsg, sign1C));
        REQUIRE(!pubkey1.Verify(hashMsg, sign2C));

        REQUIRE(!pubkey2.Verify(hashMsg, sign1));
        REQUIRE( pubkey2.Verify(hashMsg, sign2));
        REQUIRE(!pubkey2.Verify(hashMsg, sign1C));
        REQUIRE( pubkey2.Verify(hashMsg, sign2C));

        REQUIRE( pubkey1C.Verify(hashMsg, sign1));
        REQUIRE(!pubkey1C.Verify(hashMsg, sign2));
        REQUIRE( pubkey1C.Verify(hashMsg, sign1C));
        REQUIRE(!pubkey1C.Verify(hashMsg, sign2C));

        REQUIRE(!pubkey2C.Verify(hashMsg, sign1));
        REQUIRE( pubkey2C.Verify(hashMsg, sign2));
        REQUIRE(!pubkey2C.Verify(hashMsg, sign1C));
        REQUIRE( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        vector<unsigned char> csign1, csign2, csign1C, csign2C;

        REQUIRE(key1.SignCompact (hashMsg, csign1));
        REQUIRE(key2.SignCompact (hashMsg, csign2));
        REQUIRE(key1C.SignCompact(hashMsg, csign1C));
        REQUIRE(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        REQUIRE(rkey1.RecoverCompact (hashMsg, csign1));
        REQUIRE(rkey2.RecoverCompact (hashMsg, csign2));
        REQUIRE(rkey1C.RecoverCompact(hashMsg, csign1C));
        REQUIRE(rkey2C.RecoverCompact(hashMsg, csign2C));

        REQUIRE(rkey1  == pubkey1);
        REQUIRE(rkey2  == pubkey2);
        REQUIRE(rkey1C == pubkey1C);
        REQUIRE(rkey2C == pubkey2C);
    }

    // test deterministic signing

    std::vector<unsigned char> detsig, detsigc;
    string strMsg = "Very deterministic message";
    uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    REQUIRE(key1.Sign(hashMsg, detsig));
    REQUIRE(key1C.Sign(hashMsg, detsigc));
    REQUIRE(detsig == detsigc);
    REQUIRE(detsig == ParseHex("30450221009071d4fead181ea197d6a23106c48ee5de25e023b38afaf71c170e3088e5238a02200dcbc7f1aad626a5ee812e08ef047114642538e423a94b4bd6a272731cf500d0"));
    REQUIRE(key2.Sign(hashMsg, detsig));
    REQUIRE(key2C.Sign(hashMsg, detsigc));
    REQUIRE(detsig == detsigc);
    REQUIRE(detsig == ParseHex("304402204f304f1b05599f88bc517819f6d43c69503baea5f253c55ea2d791394f7ce0de02204f23c0d4c1f4d7a89bf130fed755201d22581911a8a44cf594014794231d325a"));
    REQUIRE(key1.SignCompact(hashMsg, detsig));
    REQUIRE(key1C.SignCompact(hashMsg, detsigc));
    REQUIRE(detsig == ParseHex("1b9071d4fead181ea197d6a23106c48ee5de25e023b38afaf71c170e3088e5238a0dcbc7f1aad626a5ee812e08ef047114642538e423a94b4bd6a272731cf500d0"));
    REQUIRE(detsigc == ParseHex("1f9071d4fead181ea197d6a23106c48ee5de25e023b38afaf71c170e3088e5238a0dcbc7f1aad626a5ee812e08ef047114642538e423a94b4bd6a272731cf500d0"));
    REQUIRE(key2.SignCompact(hashMsg, detsig));
    REQUIRE(key2C.SignCompact(hashMsg, detsigc));
    REQUIRE(detsig == ParseHex("1b4f304f1b05599f88bc517819f6d43c69503baea5f253c55ea2d791394f7ce0de4f23c0d4c1f4d7a89bf130fed755201d22581911a8a44cf594014794231d325a"));
    REQUIRE(detsigc == ParseHex("1f4f304f1b05599f88bc517819f6d43c69503baea5f253c55ea2d791394f7ce0de4f23c0d4c1f4d7a89bf130fed755201d22581911a8a44cf594014794231d325a"));
}

int main(int argc, char* argv[]) {
    int result = Catch::Session().run(argc, argv);
    return result;
}
