// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "json/json_file.h"
#include <string>

class CSporkMessage;
enum class SporkID;

class CSporkDB {  
 public:
  CSporkDB() = default;
  ~CSporkDB();
  bool init(const std::string& name);

 public:
  bool WriteSpork(const std::string& nSporkId, const CSporkMessage& spork);
  bool ReadSpork(const std::string& nSporkId, CSporkMessage& spork);
  bool SporkExists(const std::string& nSporkId);

  std::string path="";
  json_file jfile;
};

/** Global variable that points to the spork database (protected by cs_main) */
extern CSporkDB gSporkDB;
