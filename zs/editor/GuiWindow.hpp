#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "ImguiRenderer.hpp"
#include "zensim/TypeAlias.hpp"
#include "zensim/container/Callables.hpp"
#include "zensim/math/Vec.h"
#include "zensim/types/Mask.hpp"
#include "zensim/vulkan/Vulkan.hpp"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#if defined(ZS_PLATFORM_WINDOWS)
#  define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(ZS_PLATFORM_OSX)
#  define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "world/system/PyExecSystem.hpp"
#include "SceneEditor.hpp"
#include "interface/world/ObjectInterface.hpp"
#include "editor/widgets/GraphWidgetComponent.hpp"
#include "editor/widgets/TermWidgetComponent.hpp"
#include "editor/widgets/TextEditorComponent.hpp"
#include "editor/widgets/WidgetBase.hpp"

struct ImGuiViewport;
namespace zs {

  struct ImGuiVkWindow {
    u32 imgId;
    VkSurfaceKHR Surface;
    Owner<Swapchain> swapchain;
    Owner<RenderPass> renderPass;
    std::vector<ImguiVkRenderer::PrimitiveBuffers> bufferedData;
    std::vector<vk::CommandBuffer> cmds;
    bool ClearEnable;
    bool UseDynamicRendering;
  };
  struct ImGuiVkRendererViewportData {
    bool WindowOwned;
    ImGuiVkWindow Window;
  };

  struct ImGuiPlatformViewportData;

  ImGuiPlatformViewportData *get_platform_viewport_data(ImGuiViewport *viewport);
  int get_platform_viewport_ignore_window_pos_event_frame(ImGuiPlatformViewportData *viewport);
  int get_platform_viewport_ignore_window_size_event_frame(ImGuiPlatformViewportData *viewport);
  GLFWcursor *get_imgui_mouse_cursor(int i);
  void *get_main_viewport_states();

  /// @ref Lumos
  struct WindowConfigs {
    WindowConfigs(zs::u32 width = 1280, zs::u32 height = 720, int renderAPI = 0,
                  std::string_view title = "zpc gui window", bool fullscreen = true,
                  bool vSync = true, bool borderless = false, std::string_view filepath = "")
        : width(width),
          height(height),
          title(title),
          fullscreen(fullscreen),
          VSync(vSync),
          borderless(borderless),
          renderAPI(renderAPI),
          filePath(filepath) {}

    zs::u32 width, height;
    bool fullscreen;
    bool VSync;
    bool borderless;
    bool showConsole = true;
    std::string title;
    int renderAPI;
    std::string filePath;
    std::vector<std::string> iconPaths;
    std::vector<std::pair<u32, u8 *>> iconData;
  };

  extern GUIWindow *g_activeWindow;
  /// @ref from littleVulkanEngine GUIWindow
  struct GUIWindow {
    static GUIWindow *s_activeWindow() noexcept { return g_activeWindow; }
    static auto &s_activeGraphs() noexcept { return s_activeWindow()->states.graphs; }

    static void glfw_error_callback(int error, const char *description) {
      fprintf(stderr, "GLFW Error %d: %s\n", error, description);
    }

    GUIWindow(const WindowConfigs &);
    ~GUIWindow();

    // glfw
    bool shouldClose() { return glfwWindowShouldClose(window) || PyExecSystem::termination_requested(); }
    void pollEvents() const { glfwPollEvents(); }
    bool beginFrame();

    u32 currentImageId() const noexcept { return states.curImageId; }
    VkCommand &currentCmd() { return states.cmds[states.swapchain.get().getCurrentFrame()]; }
    VkCommand &currentCmd(u32 i) { return states.cmds[i]; }

    void tick();

    void beginRender();
    void renderFrame();
    void endRender();

    void endFrame();
    void present();

    GLFWwindow *getGlfwWindow() const noexcept { return window; }

    // plugins
    void reloadPlugins(const char *path);

    // vulkan
    VkSurfaceKHR createSurface() const;

    void maximizeWindow();
    void minimizeWindow();

    vk::Extent2D getExtent() const noexcept {
      return {static_cast<zs::u32>(states.width), static_cast<zs::u32>(states.height)};
    }
    u32 getWidth() const noexcept { return states.width; }
    u32 getHeight() const noexcept { return states.height; }
    u32 getSwapchainImageCount() const noexcept { return states.swapchain.get().imageCount(); }

    bool isMinimized() const noexcept { 
      return glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0;
      // return getWidth() == 0 && getHeight() == 0; 
    }
    bool wasWindowResized() noexcept { return states.framebufferResized; }
    void resetWindowResizedFlag() noexcept { states.framebufferResized = false; }
    float getScale() const noexcept { return states.scale; }
    float getDPIScale() const noexcept { return states.dpiScale; }

    /// glfw
    static void setup_glfw_callbacks(GLFWwindow *window);
    static void default_glfw_key_callback(GLFWwindow *window, int keycode, int scancode, int action,
                                          int mods);

    /// imgui
    void setupGUI();
    void drawGUI();
    void updateImguiMouseData();
    void updateImguiMouseCursor();

    struct InteractiveStates {
      /// window states
      int width;
      int height;
      float scale, dpiScale;
      std::string title = "";
      int focused = true;
      int monitorCount = 0;

      double time;

      u32 numBuffers() const { return swapchain.get().imageCount(); }

      VulkanContext &ctx() {
        if (pCtx == nullptr) throw std::runtime_error("vulkan context binding is not initialized!");
        return *pCtx;
      }

      /// WORLD (all temp)
      std::map<std::string, Shared<ge::Graph>> graphs;
      Shared<Terminal> terminal;
      // std::vector<ZsVar> sceneData;

      std::optional<std::string> dialogLabel;
      zs::function<void()> dialogCallback;

      /// VULKAN
      VulkanContext *pCtx{nullptr};
      Owner<Swapchain> swapchain{};
      Owner<RenderPass> rp{};
      std::vector<VkCommand> cmds;
      u32 curImageId;

      /// IMGUI_vk imgui renderer (valid after [setup])
      Owner<ImguiVkRenderer> renderer{};

      Owner<SceneEditor> sceneEditor;

      /// GLFW
      // key states
      zs::bit_mask<GLFW_KEY_LAST> keyPressed;
      GLFWwindow *keyOwnerWindows[GLFW_KEY_LAST];
      // mouse states
      zs::vec<float, 2> lastValidMousePos;
      GLFWwindow *mouseWindow;

      bool wantUpdateMonitors = false;
      bool framebufferResized = false;

      // glfw callbacks
      // window cb
      zs::callbacks<void(GLFWmonitor *, int)> monitorCb;
      zs::callbacks<void(int, int, float)> resizeCb;
      zs::callbacks<void()> closeCb;

      // key cb (int /*key*/, int /*scancode*/, int /*action*/, int /*mods*/)
      zs::callbacks<void(int /*key*/, int /*repeat*/)> keyPressedCb;
      zs::callbacks<void(int /*key*/)> keyReleasedCb;
      zs::callbacks<void(char)> textCb;

      // mouse cb (int /*button*/, int /*action*/, int /*mods*/)
      zs::callbacks<void(int /*mouse button*/)> mousePressedCb, mouseReleasedCb;
      zs::callbacks<void(int /*mouse entered*/)> mouseEnterCb;
      zs::callbacks<void(float, float)> mouseMoveCb, mouseScrollCb;

      zs::callbacks<void(int /*path_count*/, const char ** /*paths*/)> mouseDropCb;

      void updateMonitors();
    };

  protected:
    friend struct ImguiSystem;

    InteractiveStates states;
    WindowWidgetNode globalWidget;

    GLFWwindow *window;
  };

}  // namespace zs