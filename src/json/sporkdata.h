// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "json_file.h"
#include "spork.h"

class CSporkData {
 public:
  CSporkData() = default;
  ~CSporkData();

 public:
  void open();
  void WriteSpork(const std::string& name, const CSporkMessage& spork);
  bool ReadSpork(const std::string& name, CSporkMessage& spork);
  //bool SporkExists(const SporkID nSporkId) { return Exists((int)nSporkId); }

private:
  bool init_ok=false;
  std::string path;
  json_file jfile;
};
