#include <Python.h>

//
#include "world/async/Generator.hpp"
#include "world/core/Signal.hpp"
#include "world/system/ZsExecSystem.hpp"
#include "world/async/Awaitables.hpp"
#include "world/async/Coro.hpp"
#include "world/async/StateMachine.hpp"
//
#include <filesystem>

#include "GlfwSystem.hpp"
#include "GuiWindow.hpp"
#include "ImguiRenderer.hpp"
#include "ImguiSystem.hpp"
#include "imgui.h"
#include "interface/details/PyHelper.hpp"
#include "interface/world/NodeInterface.hpp"
#include "shader_runtime.h"
#include "virtual_file_helper.h"
#include "world/system/ResourceSystem.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/io/Filesystem.hpp"
#include "zensim/vulkan/Vulkan.hpp"

#define ENABLE_PROFILE 0

int main(int argc, char *argv[]) {
  using namespace zs;

  zs::IShaderManager::get().setup_default_virtual_path();

  {
    GUIWindow window{{}};
    auto &ctx = Vulkan::context(0);

    if (argc > 1)
      window.reloadPlugins(argv[1]);
    else
      window.reloadPlugins((abs_exe_directory() + "/plugins").c_str());

    zs::PyExecSystem::instance().backupPyThreadState();

    auto mainLoopCoroutine = [](GUIWindow &window) -> Future<void> {
      while (true) {
        co_await ZsExecSystem::ref_event_scheduler().schedule();
        // std::cout << "current loop thread id: " << std::this_thread::get_id() << std::endl;
#if ENABLE_PROFILE
        CppTimer timer;
        timer.tick();
#endif
        if (window.beginFrame()) {
#if ENABLE_PROFILE
          timer.tock("begin frame");
#endif

#if ENABLE_PROFILE
          timer.tick();
#endif
          window.tick();
#if ENABLE_PROFILE
          timer.tock("scene draw pass and wait");
          timer.tick();
#endif
          window.beginRender();
          window.renderFrame();
          window.endRender();
#if ENABLE_PROFILE
          timer.tock("imgui draw pass");

          timer.tick();
#endif
          window.endFrame();  // including swapchain image present
#if ENABLE_PROFILE
          timer.tock("wait imgui draw");
#endif
        } else
#if ENABLE_PROFILE
          timer.tock("skip frame");
#else
          ;
#endif
        co_await std::suspend_always();
      }
    }(window);

    while (!window.shouldClose()) {
      window.pollEvents();

      zs_execution().tick();
      ZsExecSystem::sync_process_events();  // sync wait event scheduler execution

      /// @brief GUI-related process
      if (!window.isMinimized()) {
#if 1
        mainLoopCoroutine.resume();
        ZsExecSystem::sync_process_events();
#else
        if (window.beginFrame()) {
          window.tick();

          window.beginRender();
          window.renderFrame();
          window.endRender();

          window.endFrame();  // including swapchain image present
        }
#endif
      }
      ZsExecSystem::issue_events();
    }
    zs_execution().tick();
    zs::ZsExecSystem::instance().flush();  // sync wait all schedulers
    // ZsExecSystem::sync_process_events();
  }
  // window uses the surface built upon Vulkan system, thus deinit ahead
  zs::ResourceSystem::instance().reset();
  zs::ImguiSystem::instance().reset();
  zs::GlfwSystem::instance().reset();
  zs::Vulkan::instance().reset();
  zs::ZsExecSystem::instance().reset();
  return 0;
}
