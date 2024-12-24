#include "GlfwSystem.hpp"
#include <stdexcept>
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace zs {

GlfwSystem::GlfwSystem() : _numActiveWindows{0} {
  if (!glfwInit())
    throw std::runtime_error("Unable to initialize GLFW system");

  if (!glfwVulkanSupported())
    throw std::runtime_error("Vulkan not by this GLFW!");
}

void GlfwSystem::reset() {
  if (_numActiveWindows == 0) {
    glfwTerminate();
    _numActiveWindows = -1;
  } else if (_numActiveWindows > 0)
    throw std::runtime_error("Zpc GLFW system is being destroyed while there "
                             "are still windows left");
}

GlfwSystem::~GlfwSystem() { reset(); }

void *GlfwSystem::create_window(u32 w, u32 h, std::string_view msg) {
  instance()._numActiveWindows++;
  return glfwCreateWindow(w, h, msg.data(), nullptr, nullptr);
}
void *GlfwSystem::create_window(u32 w, u32 h, void *monitor,
                                std::string_view msg) {
  instance()._numActiveWindows++;
  return glfwCreateWindow(w, h, msg.data(), (GLFWmonitor *)monitor, nullptr);
}

void GlfwSystem::destroy_window(void *window) {
  glfwDestroyWindow(static_cast<GLFWwindow *>(window));
  --instance()._numActiveWindows;

#if 0
  /// @note assume all windows are created through this system
  if (shouldClear) {
    glfwTerminate();
    instance()._numActiveWindows--;
  }
#endif
}

} // namespace zs