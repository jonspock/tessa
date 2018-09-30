#ifndef FILESYSTEM_HPP__
#define FILESYSTEM_HPP__
#pragma once
/*
        Filesystem utility library

        Copyright (c) 2015-2017 Wenzel Jakob <wenzel@inf.ethz.ch>
        Copyright (c) 2017 Josh Junon <josh@junon.me>
        Copyright (c) 2018 Wonder Media Productions

        All rights reserved. Use of this source code is governed by a
        BSD-style license that can be found in the FILESYSTEM_LICENSE file.
*/

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <sys/stat.h>

#if defined(__linux)
#include <linux/limits.h>
#endif

namespace std {
namespace filesystem {

class path;

inline bool create_directory(const path &);

/**
        \brief Simple class for manipulating paths on Linux/Windows/Mac OS

        This class is just a temporary workaround to avoid the heavy boost
        dependency until boost::filesystem is integrated into the standard template
        library at some point in the future.
*/
class path {
  friend struct std::hash<class filesystem::path>;

 public:
  typedef std::string string_type;
  typedef std::string::value_type value_type;
  typedef std::vector<string_type> leaf_list;

  enum path_type {
    windows_path = 0,
    posix_path = 1,
#if defined(_WIN32)
    native_path = windows_path
#else
    native_path = posix_path
#endif
  };

  path() : type(native_path), absolute(false) {}

  ~path() = default;

  path(const path &path) = default;

  path(path &&path) = default;

  path(const char *string) { this->set(string); }

  path(const std::string &string) { this->set(string); }

#if defined(_WIN32)
  path(const std::wstring &wstring) { this->set(wstring); }
  path(const wchar_t *wstring) { this->set(wstring); }
#endif

  path(const leaf_list &leafs) : type(native_path), leafs(leafs), absolute(false) {}

  size_t length() const { return this->leafs.size(); }

  bool empty() const { return this->leafs.empty(); }

  bool is_complete() const { return is_absolute(); }  // Should be same acc to StackExchange

  bool is_absolute() const { return this->absolute; }

  bool is_relative() const { return !this->is_absolute(); }

  leaf_list::iterator begin() { return this->leafs.begin(); }

  leaf_list::iterator end() { return this->leafs.end(); }

  const leaf_list::const_iterator cbegin() const { return this->leafs.cbegin(); }

  const leaf_list::const_iterator cend() const { return this->leafs.cend(); }

  path slice(size_t begin) const { return path(leaf_list(this->leafs.cbegin() + begin, this->leafs.cend())); }

  path slice(size_t begin, size_t end) const {
    return path(leaf_list(this->leafs.cbegin() + begin, this->leafs.cbegin() + end));
  }

  path make_absolute() const {
    if (is_absolute()) return *this;

#if !defined(_WIN32)
    // realpath only works when file exists
    if (exists()) {
      char temp[PATH_MAX];
      if (realpath(str().c_str(), temp) == NULL) {
        throw std::runtime_error("Internal error in realpath(): " + std::string(strerror(errno)));
      }

      return path(temp);
    }

    // File does not exist, prefix with current directory, and resolve
    return (getcwd() / *this).resolve();
#else
    std::wstring value = wstr(), out(MAX_PATH, '\0');
    DWORD length = GetFullPathNameW(value.c_str(), MAX_PATH, &out[0], NULL);

    if (length == 0) { throw std::runtime_error("Internal error in realpath(): " + std::to_string(GetLastError())); }

    return path(out.substr(0, length));
#endif
  }

  bool exists() const {
#if defined(_WIN32)
    return GetFileAttributesW(wstr().c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat sb;
    return stat(str().c_str(), &sb) == 0;
#endif
  }

  size_t file_size() const {
#if defined(_WIN32)
    struct _stati64 sb;

    if (_wstati64(wstr().c_str(), &sb) != 0) {
      throw std::runtime_error("path::file_size(): cannot stat file \"" + str() + "\"!");
    }

#else
    struct stat sb;

    if (stat(str().c_str(), &sb) != 0) {
      throw std::runtime_error("path::file_size(): cannot stat file \"" + str() + "\"!");
    }

#endif
    return (size_t)sb.st_size;
  }

  bool is_directory() const {
#if defined(_WIN32)
    DWORD result = GetFileAttributesW(wstr().c_str());

    if (result == INVALID_FILE_ATTRIBUTES) { return false; }

    return (result & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat sb;

    if (stat(str().c_str(), &sb)) { return false; }

    return S_ISDIR(sb.st_mode);
#endif
  }

  bool is_file() const {
#if defined(_WIN32)
    DWORD attr = GetFileAttributesW(wstr().c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    struct stat sb;

    if (stat(str().c_str(), &sb)) { return false; }

    return S_ISREG(sb.st_mode);
#endif
  }

  // TODO: Not compatible with spec!
  std::string extension() const {
    const std::string &name = filename();
    size_t pos = name.find_last_of('.');

    if (pos == std::string::npos) { return ""; }

    return name.substr(pos + 1);
  }

  path with_extension(std::string newext) {
    if (this->empty()) {
      return newext;
    } else {
      path result = dirname();

      const std::string &name = filename();
      size_t pos = name.find_last_of('.');

      result.leafs.push_back((pos == std::string::npos) ? name + newext : name.substr(0, pos) + newext);

      return result;
    }
  }

  void replace_extension(std::string newext) { set(with_extension(newext)); }

  path dirname() const {
    path result;
    result.absolute = this->absolute;

    if (this->leafs.empty()) {
      if (!this->absolute) { result.leafs.push_back(".."); }
    } else {
      size_t until = this->leafs.size() - 1;

      for (size_t i = 0; i < until; ++i) { result.leafs.push_back(this->leafs[i]); }
    }

    return result;
  }

  std::string operator[](size_t i) const { return this->leafs[i]; }

  path operator/(const path &other) const {
    if (other.absolute) { throw std::runtime_error("path::operator/(): expected a relative path!"); }

    if (this->type != other.type) { throw std::runtime_error("path::operator/(): expected a path of the same type!"); }

    path result(*this);

    for (size_t i = 0; i < other.leafs.size(); ++i) { result.leafs.push_back(other.leafs[i]); }

    return result;
  }

  const string_type &native() const {
    if (cachedNativePath.empty()) cachedNativePath = str(native_path);

    return cachedNativePath;
  }

  const string_type string() const {
    string_type s = str(posix_path);
    return s;
  }

  string_type generic_string() const { return str(posix_path); }

  const value_type *c_str() const { return native().c_str(); }

  operator string_type() const { return native(); }

  std::string str(path_type type = native_path) const {
    std::ostringstream oss;

    if (this->type == posix_path && this->absolute) { oss << "/"; }

    for (size_t i = 0; i < this->leafs.size(); ++i) {
      oss << this->leafs[i];

      if (i + 1 < this->leafs.size()) {
        if (type == posix_path) {
          oss << '/';
        } else {
          oss << '\\';
        }
      }
    }

    return oss.str();
  }

  void set(const std::string &str, path_type type = native_path) {
    this->type = type;

    if (type == windows_path) {
      this->leafs = tokenize(str, "/\\");
      this->absolute = str.size() >= 2 && std::isalpha(str[0]) && str[1] == ':';
    } else {
      this->leafs = tokenize(str, "/");
      this->absolute = !str.empty() && str[0] == '/';
    }
    this->cachedNativePath.clear();
  }

  std::string filename() const {
    if (this->empty()) { return ""; }

    return this->leafs.back();
  }

  /**
   * https://en.cppreference.com/w/cpp/filesystem/path/stem
   * Returns the filename identified by the generic-format path stripped of its extension.
   * Returns the substring from the beginning of filename() up to and not including the last period (.) character, with
   * the following exceptions: If the first character in the filename is a period, that period is ignored (a filename
   * like ".profile" is not treated as an extension) If the filename is one of the special filesystem components dot or
   * dot-dot, or if it has no periods, the function returns the entire filename().
   */
  path stem() const {
    auto last = filename();

    if (last.empty()) return path();

    const auto pos = last.find_last_of('.');

    if (pos == 0 || pos == std::string::npos || last == "..") return last;

    return last.substr(0, pos);
  }

  path resolve(bool tryabsolute = true) const {
    path result(*this);

    if (tryabsolute && this->exists()) { result = this->make_absolute(); }

    for (auto itr = result.leafs.begin(); itr != result.leafs.end();) {
      if (*itr == ".") {
        itr = result.leafs.erase(itr);
        continue;
      }

      if (*itr == "..") {
        if (itr == result.leafs.begin()) {
          if (result.absolute) {
            itr = result.leafs.erase(itr);
          } else {
            ++itr;
          }
        } else if (*(itr - 1) != "..") {
          itr = result.leafs.erase(itr - 1);
          itr = result.leafs.erase(itr);
        } else {
          ++itr;
        }

        continue;
      }

      ++itr;
    }

    return result;
  }

  path relative(const path &base) const {
    const path _base = base.resolve();
    const path _rel = this->resolve();

    if (!_rel.is_absolute()) { return _base / _rel; }

    path result;

    auto base_itr = _base.leafs.cbegin();
    auto rel_itr = _rel.leafs.cbegin();

    while (base_itr != _base.leafs.cend() || rel_itr != _rel.leafs.cend()) {
      if (base_itr == _base.leafs.cend()) {
        result.leafs.push_back(*(rel_itr++));
        continue;
      }

      if (*base_itr == *rel_itr) {
        ++base_itr;
        ++rel_itr;
        continue;
      }

      result.leafs.push_back("..");
      ++base_itr;
    }

    return result;
  }

  path resolve(const path &to) const {
    path result = this->resolve();

    if (!result.absolute) {
      throw std::runtime_error("path::resolve(): this path must be absolute, must exist so make_absolute() works");
    }

    if (this->type != result.type) { throw std::runtime_error("path::resolve(): 'to' path has different type"); }

    if (to.absolute) { return to; }

    for (std::string leaf : to.leafs) {
      if (leaf == ".") { continue; }

      if (leaf == ".." && result.leafs.size()) {
        result.leafs.pop_back();
        continue;
      }

      result.leafs.push_back(leaf);
    }

    return result;
  }

  path &operator=(const path &path) {
    this->type = path.type;
    this->leafs = path.leafs;
    this->absolute = path.absolute;
    this->cachedNativePath = path.cachedNativePath;
    return *this;
  }

  path &operator=(path &&path) {
    if (this != &path) {
      this->type = path.type;
      this->leafs = std::move(path.leafs);
      this->absolute = path.absolute;
      this->cachedNativePath = path.cachedNativePath;
    }

    return *this;
  }

  friend std::ostream &operator<<(std::ostream &os, const path &path) {
    os << path.str();
    return os;
  }

  bool remove_file() const {
#if !defined(_WIN32)
    return std::remove(str().c_str()) == 0;
#else
    return DeleteFileW(wstr().c_str()) != 0;
#endif
  }

  bool resize_file(size_t target_length) {
#if !defined(_WIN32)
    return ::truncate(str().c_str(), (off_t)target_length) == 0;
#else
    HANDLE handle = CreateFileW(wstr().c_str(), GENERIC_WRITE, 0, nullptr, 0, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (handle == INVALID_HANDLE_VALUE) { return false; }

    LARGE_INTEGER size;
    size.QuadPart = (LONGLONG)target_length;

    if (SetFilePointerEx(handle, size, NULL, FILE_BEGIN) == 0) {
      CloseHandle(handle);
      return false;
    }

    if (SetEndOfFile(handle) == 0) {
      CloseHandle(handle);
      return false;
    }

    CloseHandle(handle);
    return true;
#endif
  }

  static path getcwd() {
#if !defined(_WIN32)
    char temp[PATH_MAX];

    if (::getcwd(temp, PATH_MAX) == NULL) {
      throw std::runtime_error("Internal error in getcwd(): " + std::string(strerror(errno)));
    }

    return path(temp);
#else
    std::wstring temp(MAX_PATH, '\0');

    if (!_wgetcwd(&temp[0], MAX_PATH)) {
      throw std::runtime_error("Internal error in getcwd(): " + std::to_string(GetLastError()));
    }

    return path(temp.c_str());
#endif
  }

#if defined(_WIN32)
  std::wstring wstr(path_type type = native_path) const {
    std::string temp = str(type);
    int size = MultiByteToWideChar(CP_UTF8, 0, &temp[0], (int)temp.size(), NULL, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &temp[0], (int)temp.size(), &result[0], size);
    return result;
  }

  void set(const std::wstring &wstring, path_type type = native_path) {
    std::string string;

    if (!wstring.empty()) {
      int size = WideCharToMultiByte(CP_UTF8, 0, &wstring[0], (int)wstring.size(), NULL, 0, NULL, NULL);
      string.resize(size, 0);
      WideCharToMultiByte(CP_UTF8, 0, &wstring[0], (int)wstring.size(), &string[0], size, NULL, NULL);
    }

    this->set(string, type);
  }

  path &operator=(const std::wstring &str) {
    this->set(str);
    return *this;
  }
#endif

  bool operator==(const path &left) const { return left.leafs == this->leafs; }

  bool operator!=(const path &left) const { return left.leafs != this->leafs; }

  bool operator<(const path &right) const {
    return std::tie(this->type, this->absolute, this->leafs) < std::tie(right.type, right.absolute, right.leafs);
  }

  /*
          not the safest method! use with care!
  */
  bool mkdirp() const {
    if (!this->absolute) { throw std::runtime_error("path must be absolute to mkdirp()"); }

    if (this->exists()) { return true; }

    path parent = this->dirname();

    if (!parent.exists()) {
      if (!parent.mkdirp()) { return false; }
    }

    return create_directory(*this);
  }

  void push_back(std::string leaf) { this->leafs.push_back(leaf); }

 protected:
  static leaf_list tokenize(const std::string &string, const std::string &delim) {
    std::string::size_type lastPos = 0, pos = string.find_first_of(delim, lastPos);
    leaf_list tokens;

    if (string.empty()) {
      return tokens;  // return empty list for empty input
    }

    while (lastPos != std::string::npos) {
      if (pos != lastPos) { tokens.push_back(string.substr(lastPos, pos - lastPos)); }

      lastPos = pos;

      if (lastPos == std::string::npos || lastPos + 1 == string.length()) { break; }

      pos = string.find_first_of(delim, ++lastPos);
    }

    return tokens;
  }

 protected:
  path_type type;
  leaf_list leafs;
  bool absolute;
  mutable string_type cachedNativePath;
};

inline bool create_directory(const path &p) {
#if defined(_WIN32)
  return CreateDirectoryW(p.wstr().c_str(), NULL) != 0;
#else
  return mkdir(p.str().c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0;
#endif
}

/**
        \brief Simple class for resolving paths on Linux/Windows/Mac OS

        This convenience class looks for a file or directory given its name
        and a set of search paths. The implementation walks through the
        search paths in order and stops once the file is found.
*/
class resolver {
 public:
  typedef std::vector<path>::iterator iterator;
  typedef std::vector<path>::const_iterator const_iterator;

  resolver() { m_paths.push_back(path::getcwd()); }

  size_t size() const { return m_paths.size(); }

  iterator begin() { return m_paths.begin(); }
  iterator end() { return m_paths.end(); }

  const_iterator begin() const { return m_paths.begin(); }
  const_iterator end() const { return m_paths.end(); }

  void erase(iterator it) { m_paths.erase(it); }

  void prepend(const path &path) { m_paths.insert(m_paths.begin(), path); }
  void append(const path &path) { m_paths.push_back(path); }
  const path &operator[](size_t index) const { return m_paths[index]; }
  path &operator[](size_t index) { return m_paths[index]; }

  path resolve(const path &value) const {
    for (const_iterator it = m_paths.begin(); it != m_paths.end(); ++it) {
      path combined = *it / value;

      if (combined.exists()) { return combined; }
    }

    return value;
  }

  friend std::ostream &operator<<(std::ostream &os, const resolver &r) {
    os << "resolver[" << std::endl;

    for (size_t i = 0; i < r.m_paths.size(); ++i) {
      os << "  \"" << r.m_paths[i] << "\"";

      if (i + 1 < r.m_paths.size()) { os << ","; }

      os << std::endl;
    }

    os << "]";
    return os;
  }

 private:
  std::vector<path> m_paths;
};

inline bool create_directories(const path &p) { return p.make_absolute().mkdirp(); }

inline path temp_directory_path() {
#if defined(_WIN32)
  const auto length = GetTempPathA(0, nullptr);
  std::vector<char> buffer(length);
  GetTempPathA(length, &buffer[0]);
  const std::string tempPath(&buffer[0]);
  return tempPath;
#else
  return std::string(P_tmpdir);
#endif
}

inline bool exists(const path &path) { return path.exists(); }
}  // namespace filesystem

template <> struct hash<filesystem::path> {
  typedef filesystem::path argument_type;
  typedef std::size_t result_type;

  result_type operator()(const filesystem::path &path) const {
    std::size_t seed{0};
    hash_combine(seed, (size_t)path.type);
    hash_combine(seed, path.absolute);

    for (const string &s : path.leafs) { hash_combine(seed, s); }

    return seed;
  }

 private:
  template <class T> static inline void hash_combine(std::size_t &seed, const T &v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  }
};

namespace filesystem {
inline bool is_directory(const path &p) { return p.is_directory(); }
inline unsigned long file_size(const path &p) { return p.file_size(); }
inline bool remove(const path &p) { return p.remove_file(); }
inline path system_complete(const path &p) { return p.make_absolute(); }  // same acc to StackExchange
inline std::string extension(const path &p) { return p.extension(); }
}  // namespace filesystem
}  // namespace std
#endif
