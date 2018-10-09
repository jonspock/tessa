// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "support/allocators/secure.h"
#include <vector>

// CPrivKey : a serialized private key, with everything included
typedef std::vector<uint8_t, secure_allocator<uint8_t> > CSecretKey;

