// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sporkdb.h"
#include "fs.h"
#include "fs_utils.h"

CSporkDB::CSporkDB()  : CDataDBWrapper(GetDataDir() / "sporks", 0, false, false) {}


