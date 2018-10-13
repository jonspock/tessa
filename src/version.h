// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

/**
 * network protocol versioning
 */

static const int PROTOCOL_VERSION = 70000;

//! initial proto version, to be increased after version/verack negotiation
static const int INIT_PROTO_VERSION = 1;

//! disconnect from peers older than this proto version
static const int MIN_PEER_PROTO_VERSION_BEFORE_ENFORCEMENT = 70000;
static const int MIN_PEER_PROTO_VERSION_AFTER_ENFORCEMENT = 70000;
