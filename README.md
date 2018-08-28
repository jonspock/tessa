Tessa Core integration/staging repository
=====================================

[![Build Status](https://travis-ci.org/Tessa-Project/Tessa.svg?branch=master)](https://travis-ci.org/Tessa-Project/Tessa) [![GitHub version](https://badge.fury.io/gh/Tessa-Project%2FTessa.svg)](https://badge.fury.io/gh/Tessa-Project%2FTessa)

Tessa is an open source crypto-currency focused on fast private transactions with low transaction fees & environmental footprint.
- Anonymized transactions using the Zerocoin Protocol

Forked from PIVX SHA 732fa37 - May 16, 2018, with many changes & files from other projects such Bitcoin, Bitcoin-abc, etc

### Main code upgrades

- Removal of zerocoin based staking

- Removal of masternodes

- Removal of Instant transactions

- HD-Wallet for both main coin and zerocoin

- LMDB lightweight db for main wallet (removing Berkeley DB dependancy)

- Remove requirement for openssl

- Uses libsodium for randomization/crypto libraries

- GMP for numerics (replacing openssl)

- PoW phase uses either Argon2D or SHA256 (To be decided)

- Various options removed such as Multisig GUI. Protocol buffers and related BIP 38

- Various upgrades from Bitcoin/Bitcoin-ABC, such as logging,hd wallets,wrapping boost filesystem, etc

- Uses CMake for builds

- Reduced dependencies on Boost

- Rocksdb to replace leveldB (experimental)


### Coin Specs
<table>
<tr><td>Algo</td><td>SHA256</td></tr>
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
<tr><td>2-TBD</td><td>(200 Tessa)</td><td>N/A</td></tr>
</table>

### PoS Rewards Breakdown

<table>
<th>Phase</th><th>Block Height</th><th>Reward</th><th>Stakers</th><th>Budget</th>
<tr><td>Phase 1</td><td>TBD-TBD</td><td>50 Tessa</td><td>90% (45 Tessa)</td><td>10% (5 Tessa)</td></tr>
<tr><td>Phase X</td><td>TBD-Infinite</td><td>5 Tessa</td><td>90% (4.5 Tessa)</td><td>10% (0.5 Tessa)</td></tr>
</table>
