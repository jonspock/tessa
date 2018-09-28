// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sporkdata.h"
#include "fs.h"
#include "fs_utils.h"

void CSporkData::open() {
  path = (GetDataDir() / "sporks" / "spd.json").string();
  init_ok = jfile.open(path);
  //if (!ok) throw std::runtime_error("failed to open " + path);
}
CSporkData::~CSporkData() {
  // should dump/write json file here
  if (path == "") path = (GetDataDir() / "sporks" / "spd.json").string();
  jfile.write_json(path);
}
void CSporkData::WriteSpork(const std::string& sporkname, const CSporkMessage& spork){
  std::string sSig(spork.vchSig.begin(), spork.vchSig.end());
  jfile.json_data[sporkname] = {{"nSporkID", spork.nSporkID}, {"nValue", spork.nValue} , {"nTimeSigned", spork.nTimeSigned}, {"sSig", sSig} };
}
bool CSporkData::ReadSpork(const std::string& sporkname, CSporkMessage& spork) {
  spork.nSporkID = jfile.json_data[sporkname]["nSporkID"];
  spork.nValue = jfile.json_data[sporkname]["nValue"];
  spork.nTimeSigned = jfile.json_data[sporkname]["nTimeSigned"];
  std::string sSig = jfile.json_data[sporkname]["sSig"];
  // convert sSig to vhcSig buffer
  std::vector<uint8_t> vec(sSig.begin(), sSig.end());
  spork.vchSig =  vec;
  
  return true;
}
