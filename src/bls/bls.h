// Copyright 2018 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "aggregationinfo.h"
#include "privkey.h"
#include "pubkey.h"
#include "signature.h"
#include "extendedprivatekey.h"
#include <map>
#include <string>
#include <vector>

namespace relic {
#include "relic.h"
#include "relic_test.h"
}  // namespace relic

namespace bls12_381 {

/*
 * Principal class for verification and signature aggregation.
 * Include this file to use the library.
 */
class BLS {
 public:
  // Order of g1, g2, and gt. Private keys are in {0, GROUP_ORDER}.
  static const char GROUP_ORDER[];
  static const size_t MESSAGE_HASH_LEN = 32;

  // Initializes the BLS library manually
  static bool Init();
  // Asserts the BLS library is initialized
  static void AssertInitialized();
  // Cleans the BLS library
  static void Clean();

  // Securely aggregates many signatures on messages, some of
  // which may be identical. The signature can then be verified
  // using VerifyAggregate. The returned signature contains
  // information on how the aggregation was done (AggragationInfo).
  static Signature AggregateSigs(std::vector<Signature> const &sigs);

  // Verifies a single or aggregate signature.
  // Performs two pairing operations, sig must contain information on
  // how aggregation was performed (AggregationInfo). The Aggregation
  // Info contains all the public keys and messages required.
  static bool Verify(const Signature &sig);

  // Creates a combined public/private key that can be used to create
  // or verify aggregate signatures on the same message
  static CPubKey AggregatePubKeys(std::vector<CPubKey> const &pubKeys, bool secure);
  static CPrivKey AggregatePrivKeys(std::vector<CPrivKey> const &privateKeys,
                                         std::vector<CPubKey> const &pubKeys, bool secure);

  // Used for secure aggregation
  static void HashPubKeys(relic::bn_t *output, size_t numOutputs, std::vector<CPubKey> const &pubKeys);

 private:
  // Efficiently aggregates many signatures using the simple aggregation
  // method. Performs only n g2 operations.
  static Signature AggregateSigsSimple(std::vector<Signature> const &sigs);

  // Aggregates many signatures using the secure aggregation method.
  // Performs ~ n * 256 g2 operations.
  static Signature AggregateSigsSecure(std::vector<Signature> const &sigs,
                                          std::vector<CPubKey> const &pubKeys,
                                          std::vector<uint8_t *> const &messageHashes);

  // Internal methods
  static Signature AggregateSigsInternal(std::vector<Signature> const &sigs,
                                            std::vector<std::vector<CPubKey> > const &pubKeys,
                                            std::vector<std::vector<uint8_t *> > const &messageHashes);

  static bool VerifyNative(relic::g2_t aggSig, relic::g1_t *pubKeys, relic::g2_t *mappedHashes, size_t len);

  static void CheckRelicErrors();
};
}
