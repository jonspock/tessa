Move to Gitlab
// For PowStage
//
-testnet -staking=0  -gen=1 -printtoconsole -debug=all

spendzerocoin 1 false false 1 cFQ2QQTa7fcqbc3GgYL8eN9RNyj4jcu9So

# checkpoints in ChainParams are wrong
# check various parameters, pchMessageStart, etc

Make sure to change strMEssageMagic if coin name changes (main.cpp)

Line 33 for pow.cpp Quick Exit for testnet
Zerocoin Start 2848 main.cpp
rpc/dump 38 Locale/boost
Line 373 rpc/dump Master Key/private

Address Validator in QT bitcoinaddressvalidator is Loose

TODO
1) Aggregate BLS Signature in block so only 1 combined sig is needed
2) Review/Rethink FEES (decimal points for currency)
3) $ coins
4) Denominated Free Coins
5) Sort out Rewards - Slow start, ramp up then down
6) When -> PoS only?
7) Hybrid PoS/PoW
8) Zerocoin Enable
9) Bulletproofs
10) CLTV - Added -needs testing
11) Unit Tests
12) Wallet Init/Password Entry - QT - Mnemonic Seed?
