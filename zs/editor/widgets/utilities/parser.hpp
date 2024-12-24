#pragma once
#include <iostream>
#include <map>
#include <regex>
#include <string>

namespace zs {

  struct Lexer {
    std::string *text;
    size_t tot;
    size_t pos;
    // items of an option
    // 1. <string> short form
    // 2. <string> long form
    // 3. <string> description
    std::vector<std::vector<std::string>> dict{};
    Lexer(std::string *t) : text(t) {
      pos = 0;
      tot = t->size();
    }
    void leapSpace() {
      while (pos < tot && isspace(text->at(pos))) ++pos;
    }
    void shortOption(std::string &short_opt) {
      if (pos >= tot || text->at(pos) != '-') {
        short_opt.clear();
        return;
      }
      size_t start = pos;
      while (pos < tot && !isspace(text->at(pos)) && text->at(pos) != ',') ++pos;
      size_t len = pos - start;
      short_opt = text->substr(start, len);
    }
    void longOption(std::string &long_opt) {
      if (pos + 1 >= tot || (pos < tot && text->at(pos) != '-')
          || (pos + 1 < tot && text->at(pos + 1) != '-')) {
        long_opt.clear();
        return;
      }
      size_t start = pos;
      while (pos < tot && !isspace(text->at(pos))) ++pos;
      size_t len = pos - start;
      long_opt = text->substr(start, len);
    }
    void description(std::string &descript) {
      size_t start;
      std::regex spaces_re("\\s+");
      if ((pos < tot && text->at(pos) != '\t') && (pos < tot && text->at(pos) != '\n')
          && !(pos + 1 < tot && text->at(pos) == ' ' && text->at(pos + 1) == ' ')) {
        leapSpace();
        start = pos;
        while ((pos < tot && text->at(pos) != '\t') && (pos < tot && text->at(pos) != '\n')
               && !(pos + 1 < tot && text->at(pos) == ' ' && text->at(pos + 1) == ' '))
          ++pos;
        std::string line = text->substr(start, pos - start);
        descript += std::regex_replace(line, spaces_re, " ") + " ";
      }
      leapSpace();
      descript += " ";
      start = pos;
      while (true) {
        while (pos < tot && text->at(pos) != '\n') ++pos;
        ++pos;
        std::string line = text->substr(start, pos - start);
        descript += std::regex_replace(line, spaces_re, " ");
        if (pos < tot && text->at(pos) == '\n') break;
        leapSpace();
        if (pos >= tot || text->at(pos) == '-') break;
        start = pos;
      }
      descript.erase(std::find_if(descript.rbegin(), descript.rend(),
                                  [](unsigned char ch) { return !std::isspace(ch); })
                         .base(),
                     descript.end());
    }
    void parse() {
      bool flag = true;
      while (pos < tot) {
        if (flag && text->at(pos) == '-') break;
        if (text->at(pos) == '\n')
          flag = true;
        else if (!isspace(text->at(pos)))
          flag = false;
        ++pos;
      }
      std::string short_opt, long_opt, descript;
      while (pos < tot) {
        descript.clear();
        if (pos + 1 < tot && text->at(pos + 1) != '-') {
          shortOption(short_opt);
          descript += short_opt;
          long_opt.clear();
          if (pos < tot && text->at(pos) == ',') {
            ++pos;
            leapSpace();
            longOption(long_opt);
            descript += ", " + long_opt;
          }
          descript += " ";
        } else {
          short_opt.clear();
          longOption(long_opt);
          descript += long_opt + " ";
        }
        description(descript);
        dict.push_back(std::vector<std::string>({short_opt, long_opt, descript}));
        if (pos < tot && text->at(pos) == '\n') break;
      }
    }
    // TODO(@seeeagull): for debugging
    void printDict() {
      int i = 1;
      for (auto &it : dict) {
        std::cout << "#" << i << ":" << std::endl;
        std::cout << "  short form:  " << it[0] << std::endl;
        std::cout << "  long form:   " << it[1] << std::endl;
        std::cout << "  description: " << it[2] << std::endl;
        ++i;
      }
    }
  };

  inline std::vector<std::string> separate_string_by(const std::string &tags,
                                                     const std::string &sep) {
    std::vector<std::string> res;
    using Ti = RM_CVREF_T(std::string::npos);
    Ti st = tags.find_first_not_of(sep, 0);
    for (auto ed = tags.find_first_of(sep, st + 1); ed != std::string::npos;
         ed = tags.find_first_of(sep, st + 1)) {
      res.push_back(tags.substr(st, ed - st));
      st = tags.find_first_not_of(sep, ed);
      if (st == std::string::npos) break;
    }
    if (st != std::string::npos && st < tags.size()) {
      res.push_back(tags.substr(st));
    }
    return res;
  }

}  // namespace zs