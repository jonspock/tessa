// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "hash_map.h"
#include "json/json.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

class json_file {
 public:
  json_file() = default;
  bool open(const std::string& name);
  void write_json(const std::string& name);
  bool set_file(const std::string& name) {
    if (filename != "") return false;
    filename = name;
    return true;
  }
  template <typename T_> void write(const std::string& k, T_ v) { json_data[k] = v; };
  template <typename T_> void write(int k, T_ v) { json_data[k] = v; };
  nlohmann::json& get_json() { return json_data; }

 private:
  std::string filename = "";
  std::ifstream m_inf;
  std::ofstream m_outf;
  hash_map<std::string> m_table;
public:
  nlohmann::json json_data;

 public:
  void read_file();
  void print();
  void debug_print();
  bool find(const std::string& name, std::string& line);
  bool exists(const std::string& name) {   return m_table.contains(name); }


  template <typename T_> T_ from_string(const std::string& name) { return name; }
  template <typename T_> void get_param(const std::string& name, T_& val) {
    std::string val_str = m_table.getValue(name);
    val = from_string<T_>(val_str);
  }
  template <typename T_> T_ get_param(const std::string& name) {
    T_ val;
    get_param(name, val);
    return val;
  }
  std::vector<std::string> get_array_strings(const std::string& line);
};

template <> inline double json_file::from_string(const std::string& name) { return (double)std::stod(name); }
template <> inline int8_t json_file::from_string(const std::string& name) { return (int8_t)std::stoi(name); }
template <> inline int16_t json_file::from_string(const std::string& name) { return (int16_t)std::stoi(name); }
template <> inline int32_t json_file::from_string(const std::string& name) { return (int32_t)std::stoi(name); }
template <> inline int64_t json_file::from_string(const std::string& name) { return (int64_t)std::stoi(name); }
template <> inline uint8_t json_file::from_string(const std::string& name) { return (uint8_t)std::stoi(name); }
template <> inline uint16_t json_file::from_string(const std::string& name) { return (uint16_t)std::stoi(name); }
template <> inline uint32_t json_file::from_string(const std::string& name) { return (uint32_t)std::stoi(name); }
template <> inline uint64_t json_file::from_string(const std::string& name) { return (uint64_t)std::stoi(name); }
