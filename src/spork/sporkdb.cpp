// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sporkdb.h"
#include "spork/spork.h"
#include "json/json_file.h"
#include "logging.h"

// Global variable of State
CSporkDB gSporkDB;

bool CSporkDB::init(const std::string& name) {
  path = name;
  return jfile.open(name);
}
CSporkDB::~CSporkDB() { if (path != "") jfile.write_json(path); }

bool CSporkDB::WriteSpork(const std::string& sporkname, const CSporkMessage& spork) {
  LogPrint(TessaLog::SPORK, "Wrote spork %s to database\n", sporkname);
  std::string sSig(spork.vchSig.begin(), spork.vchSig.end());
  jfile.json_data[sporkname] = {
      {"nSporkID", spork.nSporkID}, {"nValue", spork.nValue}, {"nTimeSigned", spork.nTimeSigned}, {"sSig", sSig}};
  return true;
}
bool CSporkDB::ReadSpork(const std::string& sporkname, CSporkMessage& spork) {
  if (SporkExists(sporkname)) {
    spork.nSporkID = jfile.json_data[sporkname]["nSporkID"];
    spork.nValue = jfile.json_data[sporkname]["nValue"];
    spork.nTimeSigned = jfile.json_data[sporkname]["nTimeSigned"];
    std::string sSig = jfile.json_data[sporkname]["sSig"];
    // convert sSig to vhcSig buffer
    std::vector<uint8_t> vec(sSig.begin(), sSig.end());
    spork.vchSig = vec;
    return true;
  } else {
    // Set as disabled for future usage
    jfile.json_data[sporkname] = {{"nSporkID", 0}, {"nValue", 0}, {"nTimeSigned", 0}, {"sSig", ""}};
    return false;
  }
}
bool CSporkDB::SporkExists(const std::string& sporkname) { return jfile.exists(sporkname); }
