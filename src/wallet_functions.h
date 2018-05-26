// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers 
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include <map>
#include <string>
#include "amount.h"

typedef std::map<std::string, std::string> mapValue_t;

bool MoneyRange(CAmount nValueOut);
void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue);
void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue);
