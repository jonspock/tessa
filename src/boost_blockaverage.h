// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "streams.h"
#include "logging.h"
#include <stdexcept>
#include <boost/circular_buffer.hpp>

/**
 * Keep track of fee/priority for transactions confirmed within N blocks
 */
class CBlockAverage {
 private:
  boost::circular_buffer<double> prioritySamples;

  template <typename T> std::vector<T> buf2vec(boost::circular_buffer<T> buf) const {
    std::vector<T> vec(buf.begin(), buf.end());
    return vec;
  }

 public:
  CBlockAverage() : prioritySamples(100) {}

  void RecordPriority(double priority) { prioritySamples.push_back(priority); }

  size_t PrioritySamples() const { return prioritySamples.size(); }
  size_t GetPrioritySamples(std::vector<double>& insertInto) const {
    for (double d : prioritySamples) insertInto.push_back(d);
    return prioritySamples.size();
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
    std::vector<double> vecPriority = buf2vec(prioritySamples);
    fileout << vecPriority;
  }

  void Read(CAutoFile& filein) {
    std::vector<double> vecPriority;
    filein >> vecPriority;
    if (AreSane(vecPriority))
      prioritySamples.insert(prioritySamples.end(), vecPriority.begin(), vecPriority.end());
    else
      throw runtime_error("Corrupt priority value in estimates file.");
    if (prioritySamples.size() > 0)
      LogPrint(TessaLog::ESTIMATEFEE, "Read %d priority samples\n", prioritySamples.size());
  }
};

