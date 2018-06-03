/**
 * @file       Coin.cpp
 *
 * @brief      PublicCoin and PrivateCoin classes for the Zerocoin library.
 *
 * @author     Ian Miers, Christina Garman and Matthew Green
 * @date       June 2013
 *
 * @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
 * @license    This project is released under the MIT license.
 **/
// Copyright (c) 2017-2018 The PIVX developers 
// Copyright (c) 2018 The ClubChain developers

#include <stdexcept>
#include <iostream>
#include "Coin.h"
#include "Commitment.h"
#include "pubkey.h"

namespace libzerocoin {

//PublicCoin class
PublicCoin::PublicCoin(const ZerocoinParams* p):
	params(p) {
	if (this->params->initialized == false) {
		throw std::runtime_error("Params are not initialized");
	}
    // Assume this will get set by another method later
    denomination = ZQ_ERROR;
};

PublicCoin::PublicCoin(const ZerocoinParams* p, const CBigNum& coin, const CoinDenomination d):
	params(p), value(coin) {
	if (this->params->initialized == false) {
		throw std::runtime_error("Params are not initialized");
	}

	denomination = d;
	for(const CoinDenomination denom : zerocoinDenomList) {
		if(denom == d)
			denomination = d;
	}
    if(denomination == 0){
		std::cout << "denom does not exist\n";
		throw std::runtime_error("Denomination does not exist");
	}
};

bool PublicCoin::validate() const
{
    if (this->params->accumulatorParams.minCoinValue >= value) {
        cout << "PublicCoin::validate value is too low\n";
        return false;
    }

    if (value > this->params->accumulatorParams.maxCoinValue) {
        cout << "PublicCoin::validate value is too high\n";
        return false;
    }

    if (!value.isPrime(params->zkp_iterations)) {
        cout << "PublicCoin::validate value is not prime\n";
        return false;
    }

    return true;
}

//PrivateCoin class
PrivateCoin::PrivateCoin(const ZerocoinParams* p): params(p), publicCoin(p) {
	// Verify that the parameters are valid
	if(this->params->initialized == false) {
		throw std::runtime_error("Params are not initialized");
	}

  this->version = PRIVATECOIN_VERSION;
}

PrivateCoin::PrivateCoin(const ZerocoinParams* p, const CoinDenomination denomination, const CBigNum& bnSerial,
                         const CBigNum& bnRandomness): params(p), publicCoin(p)
{
        // Verify that the parameters are valid
    if(!this->params->initialized)
        throw std::runtime_error("Params are not initialized");

    this->serialNumber = bnSerial;
    this->randomness = bnRandomness;

    Commitment commitment(&p->coinCommitmentGroup, bnSerial, bnRandomness);
    this->publicCoin = PublicCoin(p, commitment.getCommitmentValue(), denomination);
}

bool PrivateCoin::IsValid()
{
    if (!IsValidSerial(params, serialNumber)) {
        cout << "Serial not valid\n";
        return false;
    }

    return getPublicCoin().validate();
}

bool GenerateKeyPair(const CBigNum& bnGroupOrder, const uint256& nPrivkey, CKey& key, CBigNum& bnSerial)
{
    // Generate a new key pair, which also has a 256-bit pubkey hash that qualifies as a serial #
    // This builds off of Tim Ruffing's work on libzerocoin, but has a different implementation
    CKey keyPair;
    if (nPrivkey == 0)
        keyPair.MakeNewKey(true);
    else
        keyPair.Set(nPrivkey.begin(), nPrivkey.end(), true);

    CPubKey pubKey = keyPair.GetPubKey();
    uint256 hashPubKey = Hash(pubKey.begin(), pubKey.end());


    CBigNum s(hashPubKey);
    uint256 nBits = hashPubKey >> 252; // must be less than 0x0D to be valid serial range
    if (nBits > 12)
        return false;

    key = keyPair;
    bnSerial = s;
    return true;
}

const CPubKey PrivateCoin::getPubKey() const
{
	CKey key;
	key.SetPrivKey(privkey, true);
	return key.GetPubKey();
}

bool PrivateCoin::sign(const uint256& hash, vector<unsigned char>& vchSig) const
{
	CKey key;
	key.SetPrivKey(privkey, true);
	return key.Sign(hash, vchSig);
}

int ExtractVersionFromSerial(const CBigNum& bnSerial)
{
	return 1;
}

CBigNum GetAdjustedSerial(const CBigNum& bnSerial)
{
    uint256 serial = bnSerial.getuint256();
    CBigNum bnSerialAdjusted;
    bnSerialAdjusted.setuint256(serial);
    return bnSerialAdjusted;
}


bool IsValidSerial(const ZerocoinParams* params, const CBigNum& bnSerial)
{
    if (bnSerial <= 0)
        return false;

    return bnSerial < params->coinCommitmentGroup.groupOrder;
}

} /* namespace libzerocoin */
