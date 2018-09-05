// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once
#include "mainfile.h"
#include <string>

/** Capture information about block/transaction validation */
class CValidationState {
 private:
  enum mode_state {
    MODE_VALID,    //! everything ok
    MODE_INVALID,  //! network rule violation (DoS value may be set)
    MODE_ERROR,    //! run-time error
  } mode;
  int nDoS;
  std::string strRejectReason;
  uint8_t chRejectCode;
  bool corruptionPossible;

 public:
  CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(0), corruptionPossible(false) {}
  bool DoS(int level, bool ret = false, uint8_t chRejectCodeIn = 0, const std::string& strRejectReasonIn = "",
           bool corruptionIn = false) {
    chRejectCode = chRejectCodeIn;
    strRejectReason = strRejectReasonIn;
    corruptionPossible = corruptionIn;
    if (mode == MODE_ERROR) return ret;
    nDoS += level;
    mode = MODE_INVALID;
    return ret;
  }
  bool Invalid(bool ret = false, uint8_t _chRejectCode = 0, const std::string& _strRejectReason = "") {
    return DoS(0, ret, _chRejectCode, _strRejectReason);
  }
  bool Error(const std::string& strRejectReasonIn = "") {
    if (mode == MODE_VALID) strRejectReason = strRejectReasonIn;
    mode = MODE_ERROR;
    return false;
  }
  bool Abort(const std::string& msg) {
    AbortNode(msg);
    return Error(msg);
  }
  bool IsValid() const { return mode == MODE_VALID; }
  bool IsInvalid() const { return mode == MODE_INVALID; }
  bool IsError() const { return mode == MODE_ERROR; }
  bool IsInvalid(int& nDoSOut) const {
    if (IsInvalid()) {
      nDoSOut = nDoS;
      return true;
    }
    return false;
  }
  bool CorruptionPossible() const { return corruptionPossible; }
  uint8_t GetRejectCode() const { return chRejectCode; }
  std::string GetRejectReason() const { return strRejectReason; }
};
