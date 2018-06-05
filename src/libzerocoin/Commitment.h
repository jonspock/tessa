/**
 * @file       Commitment.h
 *
 * @brief      Commitment and CommitmentProof classes for the Zerocoin library.
 *
 * @author     Ian Miers, Christina Garman and Matthew Green
 * @date       June 2013
 *
 * @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
 *  license    This project is released under the MIT license.
 **/
// Copyright (c) 2018 The PIVX developer
// Copyright (c) 2018 The ClubChain developers
#pragma once
#include "IntegerMod.h"
#include "ModulusType.h"
#include "ZerocoinParams.h"
#include "serialize.h"
namespace libzerocoin {

/**
 * A commitment, complete with contents and opening randomness.
 * These should remain secret. Publish only the commitment value.
 */
class Commitment {
 public:
  Commitment(const CBigNum& r, const CBigNum& v, const CBigNum c) {
    randomness = r;
    contents = v;
    commitmentValue = c;
  }
  const CBigNum& getCommitmentValue() const { return this->commitmentValue; }
  const CBigNum& getRandomness() const { return this->randomness; }
  const CBigNum& getContents() const { return this->contents; }

 private:
  CBigNum commitmentValue;
  CBigNum randomness;
  CBigNum contents;

  ADD_SERIALIZE_METHODS;
  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
    READWRITE(commitmentValue);
    READWRITE(randomness);
    READWRITE(contents);
  }

 public:
};

} /* namespace libzerocoin */
