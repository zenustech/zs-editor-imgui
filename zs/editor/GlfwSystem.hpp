#pragma once
#include "zensim/TypeAlias.hpp"
#include <string_view>

namespace zs {

struct GlfwSystem {
private:
  GlfwSystem();

public:
  static GlfwSystem &instance() {
    static GlfwSystem s_instance{};
    return s_instance;
  }
  static void initialize() { (void)instance(); }
  static void *create_window(u32 w, u32 h, std::string_view msg);
  static void *create_window(u32 w, u32 h, void *monitor, std::string_view msg);
  static void destroy_window(void *);

  static int num_windows() noexcept { return instance()._numActiveWindows; }
  ~GlfwSystem();

  void reset();

  int _numActiveWindows;
};

} // namespace zs