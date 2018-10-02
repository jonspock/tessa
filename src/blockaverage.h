// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "logging.h"
#include "streams.h"
#include <stdexcept>

/**
 * Keep track of fee/priority for transactions confirmed within N blocks
 */
class CBlockAverage {
 private:
  static const int cb_size = 100;
  double prioritySamples[cb_size];
  int cb_index = 0;
  int current_size = 0;

 public:
  CBlockAverage() {}

  void RecordPriority(double priority) {
    prioritySamples[cb_index++] = priority;
    if (cb_index == cb_size) cb_index = 0;
    if (current_size != cb_size) current_size++;
  }
  size_t size() const { return current_size; }
  size_t GetPrioritySamples(std::vector<double>& insertInto) const {
    if (current_size < cb_size) {
      for (int i = 0; i < current_size; i++) insertInto.push_back(prioritySamples[i]);
    } else {
      for (int i = 0; i < cb_size; i++) {
        int index = cb_index + i;
        if (index > cb_size) index -= cb_size;
        double val = prioritySamples[index];
        insertInto.push_back(val);
      }
    }
    return current_size;
  }

  /**
   * Used as belt-and-suspenders check when reading to detect
   * file corruption
   */
  static bool AreSane(const double priority) { return priority >= 0; }
  static bool AreSane(const std::vector<double> vecPriority) {
    for (double priority : vecPriority) {
      if (!AreSane(priority)) return false;
    }
    return true;
  }

  void Write(CAutoFile& fileout) const {
    std::vector<double> vec;
    GetPrioritySamples(vec);
    fileout << vec;
  }

  void Read(CAutoFile& filein) {
    std::vector<double> vecPriority;
    filein >> vecPriority;
    if (AreSane(vecPriority))
      for (size_t i = 0; i < vecPriority.size(); i++) RecordPriority(vecPriority[i]);
    else
      throw std::runtime_error("Corrupt priority value in estimates file.");
    if (size() > 0) LogPrint(TessaLog::ESTIMATEFEE, "Read %d priority samples\n", size());
  }
};
