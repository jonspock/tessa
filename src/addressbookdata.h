#pragma once
#include <map>
#include <string>

/** Address book data */
class CAddressBookData {
 public:
  std::string name;
  std::string purpose;

  CAddressBookData() { purpose = "unknown"; }

  typedef std::map<std::string, std::string> StringMap;
  StringMap destdata;
};
