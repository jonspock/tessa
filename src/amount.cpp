// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "tinyformat.h"

std::string CFeeRate::ToString() const { return strprintf("%d.%08d Tessa/kB", fee / COIN, fee % COIN); }
