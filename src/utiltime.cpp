// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "coin-config.h"
#endif

#include "tinyformat.h"
#include "utiltime.h"

#include <chrono>
#include <iomanip>  // for put_time
#include <thread>

using namespace std;

static int64_t nMockTime = 0;  //! For unit testing

int64_t GetTime() {
  if (nMockTime) return nMockTime;

  time_t now = time(nullptr);
  assert(now > 0);
  return now;
}

void SetMockTime(int64_t nMockTimeIn) { nMockTime = nMockTimeIn; }

int64_t GetTimeMillis() {
  std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  return millis;
}

int64_t GetTimeMicros() {
  std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  return micros;
}

int64_t GetSystemTimeInSeconds() { return GetTimeMicros() / 1000000; }

/** Return a time useful for the debug log */
int64_t GetLogTimeMicros() {
  if (nMockTime) return nMockTime * 1000000;

  return GetTimeMicros();
}

void MilliSleep(int64_t n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }

std::string DateTimeStrFormat(const char *pszFormat, int64_t nTime) {
  std::chrono::system_clock::time_point tp = std::chrono::system_clock::from_time_t(nTime);
  std::time_t ttp = std::chrono::system_clock::to_time_t(tp);
  static std::locale classic(std::locale::classic());
  // std::locale takes ownership of the pointer
  // std::locale loc(classic, new boost::posix_time::time_facet(pszFormat));
  std::stringstream ss;
  ss.imbue(classic);  // was loc.
  ss << std::put_time(std::localtime(&ttp), pszFormat);
  return ss.str();
}

std::string DurationToDHMS(int64_t nDurationTime) {
  int seconds = nDurationTime % 60;
  nDurationTime /= 60;
  int minutes = nDurationTime % 60;
  nDurationTime /= 60;
  int hours = nDurationTime % 24;
  int days = nDurationTime / 24;
  if (days) return strprintf("%dd %02dh:%02dm:%02ds", days, hours, minutes, seconds);
  if (hours) return strprintf("%02dh:%02dm:%02ds", hours, minutes, seconds);
  return strprintf("%02dm:%02ds", minutes, seconds);
}
