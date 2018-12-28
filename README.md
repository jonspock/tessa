Tessa Core integration/staging repository
=====================================

[![Build Status](https://travis-ci.org/Tessa-Project/Tessa.svg?branch=master)](https://travis-ci.org/Tessa-Project/Tessa) [![GitHub version](https://badge.fury.io/gh/Tessa-Project%2FTessa.svg)](https://badge.fury.io/gh/Tessa-Project%2FTessa)

Tessa is an open source crypto-currency focused on private transactions - Anonymized using the Zerocoin Protocol

- Using BLS Signatures for Compact Size and other features below

This repo contains code from Bitcoin, Monero, Dash, Peercoin, Bitcoin-ABC, Chia and PIVX from various point in the past.
Original tree was forked from PIVX SHA 732fa37 - May 16, 2018, but there has been so many changes,
it's unlikely to have much resemblance to that code branch.

### Main code upgrades

- Removal of zerocoin based staking (PIVX)

- Removal of masternodes (Dash)

- Removal of Instant transactions (Dash)

- HD-Wallet for both main coin and zerocoin

- LMDB lightweight db for main wallet (removing Berkeley DB dependancy) (from Monero)

- Remove requirement for openssl

- Remove of secp256k1/ECDSA keys

- Uses libsodium for randomization/crypto libraries

- Uses relic library for cryptographic primitives

- GMP for numerics (replacing openssl)

- PoW phase uses either Argon2D or SHA256 (To be decided)

- BCH32 for Address codes (from Bitcoin's BECH32)

- BLS library from Chia for Signatures

- Various options removed such as Multisig GUI. Protocol buffers and related BIP 38 (from PIVX)

- Various upgrades from Bitcoin/Bitcoin-ABC, such as logging,hd wallets,wrapping boost filesystem, etc

- Uses CMake for build

- Rocksdb to replace leveldB (experimental)

- Reduced dependencies on Boost

- Extensive usage of C++11's for range loops, std::thread, std:bind, std::mutex, etc to replace boost library

- Usage of C++17 std::variant library and std::filesystem substitutes for boost features

- Inability to have un-encrypted Wallets

- Change in Fee handling

- Usage of signals-cpp library substitute for boost signals2

- Multiple Licenses, including MIT for Bitcoin/Dash/Peercoin/PIVX/Signals-cpp code, Apache License 2.0 for RELIC and Chia code

### Coin Specs
<table>
<tr><td>Algo</td><td>SHA256 or Argon2D </td></tr>
<tr><td>Block Time</td><td>60 Seconds</td></tr>
<tr><td>Difficulty Retargeting</td><td>Every Block</td></tr>
<tr><td>Max Coin Supply (PoW Phase)</td><td>TBD Tessa</td></tr>
<tr><td>Max Coin Supply (PoS Phase)</td><td>Infinite</td></tr>
</table>


### Reward Distribution

<table>
<th colspan=4>Genesis Block</th>
<tr><th>Block Height</th><th>Reward Amount</th><th>Notes</th></tr>
</table>

### PoW Rewards Breakdown

<table>
<th>Block Height</th><th>Miner</th><th>Budget</th>
<tr><td>2-TBD</td><td>TBD</td><td>N/A</td></tr>
</table>

### PoS Rewards Breakdown

<table>
<th>Phase</th><th>Block Height</th><th>Reward</th>
<tr><td>Phase 1</td><td>TBD-TBD</td><td>TBD Tessa</td></tr>
<tr><td>Phase X</td><td>TBD-Infinite</td><td>TBD Tessa</tr>
</table>
