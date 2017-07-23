#ifndef _STUFF_UTILS_H_
#define _STUFF_UTILS_H_

#include <fstream>
#include <string>

namespace stuff {

inline std::string ReadFile(const char* fname) {
  std::ifstream is(fname);
  std::string s;
  do {
    const size_t sz = s.size();
    constexpr size_t kN = 2048;
    s.resize(sz + kN);
    is.read(&s.front() + sz, kN);
    s.resize(sz + is.gcount());
  } while (is);
  return s;
}

}  // namespace stuff

#endif  // _STUFF_UTILS_H_
