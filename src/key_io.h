// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <chainparams.h>
#include <ecdsa/key.h>
#include <ecdsa/pubkey.h>
#include <script/standard.h>

#include <string>

ecdsa::CKey DecodeSecret(const std::string& str);
std::string EncodeSecret(const ecdsa::CKey& key);

ecdsa::CExtKey DecodeExtKey(const std::string& str);
std::string EncodeExtKey(const ecdsa::CExtKey& extkey);
ecdsa::CExtPubKey DecodeExtPubKey(const std::string& str);
std::string EncodeExtPubKey(const ecdsa::CExtPubKey& extpubkey);

std::string EncodeDestination(const CTxDestination& dest);
CTxDestination DecodeDestination(const std::string& str);
bool IsValidDestinationString(const std::string& str);
bool IsValidDestinationString(const std::string& str, const CChainParams& params);

