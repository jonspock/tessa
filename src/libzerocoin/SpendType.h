// Copyright (c) 2018 The PIVX developers 
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <cstdint>

namespace libzerocoin {
    enum SpendType : uint8_t {
        SPEND, // Used for a typical spend transaction, zZZZ should be unusable after
        SIGN_MESSAGE // Used to sign messages that do not belong above (future)
    };
}
