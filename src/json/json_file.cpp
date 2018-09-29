// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "json_file.h"
#include "logging.h"
using namespace nlohmann;
using std::string;


bool json_file::open(const string& name) {
  filename = name;
  m_inf.open(name, std::ifstream::in);
  if (!m_inf) {
    LogPrintf("Unable to open input file %s\n",name);
    return false;
  }
  read_file();
  return true;
}

void json_file::read_file() {
  std::stringstream buffer;
  buffer << m_inf.rdbuf();
  std::string s = buffer.str();
  json o = json::parse(s);
  for (json::iterator it = o.begin(); it != o.end(); ++it) {
    std::cout << it.key() << " : " << it.value() << "\n";
    std::stringstream s;
    s << it.value();
    m_table.add(it.key(), s.str());
  }
  m_inf.close();
}

bool json_file::find(const string& name, string& param_str) {
  if (!m_table.contains(name)) { return false; }
  param_str = m_table.getValue(name);
  return true;
}
void json_file::debug_print() {
  for (nlohmann::json::iterator it = json_data.begin(); it != json_data.end(); ++it) {
    std::stringstream name;
    name << it.key();
    switch (it.value().type()) {
    case nlohmann::json::value_t::string: {
      std::string s = it.value();
      std::cout << "name = " << name.str() << " s = " << s << "\n";
    } break;
    default: {
      double val = it.value();
      std::cout << "name = " << name.str() << " val = " << val << "\n";
    } break;
    }
  }
}

void json_file::print() {
  std::cout << " List of keys found with values\n";
  for (auto& b : m_table.table) std::cout << b.first << " = " << m_table.getValue(b.first) << "\n";
}

std::vector<std::string> json_file::get_array_strings(const std::string& line) {
  std::vector<std::string> vals;
  size_t pos = 0;
  size_t last_pos = 0;

  int num_vals = 0;
  while ((pos = line.find_first_not_of(",", last_pos)) != string::npos) {
    last_pos = line.find_first_of(",", pos);
    if (last_pos == string::npos) { last_pos = line.length(); }
    int len = last_pos - pos;
    std::string val_str = line.substr(pos, len);
    num_vals += 1;
    vals.push_back(val_str);
  }
  return vals;
}
void json_file::write_json(const std::string& name) {
  std::ofstream file_;
  file_.open(name.c_str(), std::ofstream::out);
  file_ << json_data.dump(4);
  file_.close();
}
 

