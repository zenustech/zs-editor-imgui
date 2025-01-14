#include "GuiWindow.hpp"

#include "GlfwSystem.hpp"
#include "ImguiSystem.hpp"
#include "imgui.h"
#include "interface/world/value_type/ValueInterface.hpp"
#include "python/Init.hpp"
#include "world/World.hpp"
#include "world/system/ResourceSystem.hpp"
#include "zensim/io/MeshIO.hpp"
#include "zensim/vulkan/Vulkan.hpp"

#if defined(VK_VERSION_1_3) || defined(VK_KHR_dynamic_rendering)
#  define IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING
static PFN_vkCmdBeginRenderingKHR ImGuiImplVulkanFuncs_vkCmdBeginRenderingKHR;
static PFN_vkCmdEndRenderingKHR ImGuiImplVulkanFuncs_vkCmdEndRenderingKHR;
#endif

namespace zs {

  ////
  //// imgui platform ui setup
  ////
  namespace {
    static void *g_prevWndProc = nullptr;
  }  // namespace
  static GLFWcursor *g_MouseCursors[ImGuiMouseCursor_COUNT] = {0};
  GLFWcursor *get_imgui_mouse_cursor(int i) { return g_MouseCursors[i]; }

  struct ImGuiPlatformViewportData {
    GLFWwindow *Window;
    GUIWindow::InteractiveStates *States;
    bool WindowOwned;
    int IgnoreWindowPosEventFrame;
    int IgnoreWindowSizeEventFrame;
#if defined(ZS_PLATFORM_WINDOWS)
    WNDPROC PrevWndProc;
#endif

    GUIWindow::InteractiveStates &states() { return *States; }
    VulkanContext &ctx() {
      if (States == nullptr)
        throw std::runtime_error("gui window states binding is not initialized!");
      return States->ctx();
    }
    ImGuiPlatformViewportData() {
      std::memset(this, 0, sizeof(*this));
      IgnoreWindowSizeEventFrame = IgnoreWindowPosEventFrame = -1;
    }
    ~ImGuiPlatformViewportData() { IM_ASSERT(Window == nullptr); }
  };

  ImGuiPlatformViewportData *get_platform_viewport_data(ImGuiViewport *viewport) {
    return (ImGuiPlatformViewportData *)viewport->PlatformUserData;
  }
  int get_platform_viewport_ignore_window_pos_event_frame(ImGuiPlatformViewportData *vd) {
    return vd->IgnoreWindowPosEventFrame;
  }
  int get_platform_viewport_ignore_window_size_event_frame(ImGuiPlatformViewportData *vd) {
    return vd->IgnoreWindowSizeEventFrame;
  }
  void *get_main_viewport_states() {
    auto mainViewport = ImGui::GetMainViewport();
    return ((ImGuiPlatformViewportData *)(mainViewport->PlatformUserData))->States;
  }

#if defined(ZS_PLATFORM_WINDOWS)
  static ImGuiMouseSource GetMouseSourceFromMessageExtraInfo() {
    LPARAM extra_info = ::GetMessageExtraInfo();
    if ((extra_info & 0xFFFFFF80) == 0xFF515700) return ImGuiMouseSource_Pen;
    if ((extra_info & 0xFFFFFF80) == 0xFF515780) return ImGuiMouseSource_TouchScreen;
    return ImGuiMouseSource_Mouse;
  }
  static LRESULT CALLBACK imgui_wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WNDPROC prev_wndproc = (WNDPROC)g_prevWndProc;
    ImGuiViewport *viewport = (ImGuiViewport *)::GetPropA(hWnd, "IMGUI_VIEWPORT");
    if (viewport != NULL)
      if (ImGuiPlatformViewportData *vd = (ImGuiPlatformViewportData *)viewport->PlatformUserData)
        prev_wndproc = vd->PrevWndProc;

    switch (msg) {
        // GLFW doesn't allow to distinguish Mouse vs TouchScreen vs Pen.
        // Add support for Win32 (based on imgui_impl_win32), because we rely on
        // _TouchScreen info to trickle inputs differently.
      case WM_MOUSEMOVE:
      case WM_NCMOUSEMOVE:
      case WM_LBUTTONDOWN:
      case WM_LBUTTONDBLCLK:
      case WM_LBUTTONUP:
      case WM_RBUTTONDOWN:
      case WM_RBUTTONDBLCLK:
      case WM_RBUTTONUP:
      case WM_MBUTTONDOWN:
      case WM_MBUTTONDBLCLK:
      case WM_MBUTTONUP:
      case WM_XBUTTONDOWN:
      case WM_XBUTTONDBLCLK:
      case WM_XBUTTONUP:
        ImGui::GetIO().AddMouseSourceEvent(GetMouseSourceFromMessageExtraInfo());
        break;

        // We have submitted https://github.com/glfw/glfw/pull/1568 to allow GLFW to
        // support "transparent inputs". In the meanwhile we implement custom
        // per-platform workarounds here (FIXME-VIEWPORT: Implement same work-around
        // for Linux/OSX!)
#  if !GLFW_HAS_MOUSE_PASSTHROUGH && GLFW_HAS_WINDOW_HOVERED
      case WM_NCHITTEST: {
        // Let mouse pass-through the window. This will allow the backend to call
        // io.AddMouseViewportEvent() properly (which is OPTIONAL). The
        // ImGuiViewportFlags_NoInputs flag is set while dragging a viewport, as
        // want to detect the window behind the one we are dragging. If you cannot
        // easily access those viewport flags from your windowing/event code: you
        // may manually synchronize its state e.g. in your main loop after calling
        // UpdatePlatformWindows(). Iterate all viewports/platform windows and pass
        // the flag to your windowing system.
        if (viewport && (viewport->Flags & ImGuiViewportFlags_NoInputs)) return HTTRANSPARENT;
        break;
      }
#  endif
    }
    return ::CallWindowProc(prev_wndproc, hWnd, msg, wParam, lParam);
  }

#endif

  ////
  //// GUIWindow setup
  ////
  GUIWindow *g_activeWindow = nullptr;

  GUIWindow::GUIWindow(const WindowConfigs &configs) {
    g_activeWindow = this;
    ///
    /// glfw
    ///
    (void)zs::GlfwSystem::initialize();
    glfwSetErrorCallback(glfw_error_callback);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

    GLFWmonitor **glfw_monitors = glfwGetMonitors(&states.monitorCount);
    if (states.monitorCount == 0) return;

    GLFWmonitor *monitor = glfwGetPrimaryMonitor();

    float xscale, yscale;
    // glfwGetMonitorContentScale(monitor, &xscale, &yscale);
    // states.dpiScale = xscale;

    if (configs.fullscreen) {
      int xpos, ypos, width, height;
#ifdef ZS_PLATFORM_WINDOWS
      RECT WorkArea;
#  if 1
      if (SystemParametersInfo(SPI_GETWORKAREA, 0, &WorkArea, FALSE) == TRUE) {
        states.width = WorkArea.right - WorkArea.left;
        states.height = WorkArea.bottom - WorkArea.top;
      }
#  else
      GetWindowRect(GetDesktopWindow(), &WorkArea);
      states.width = WorkArea.right - WorkArea.left;
      states.height = WorkArea.bottom - WorkArea.top;
#  endif
#else
      glfwGetMonitorWorkarea(monitor, &xpos, &ypos, &width, &height);
      states.width = width;
      states.height = height;
#endif
      // const GLFWvidmode *mode = glfwGetVideoMode(monitor);
      // states.width = mode->width;
      // states.height = mode->height;
    } else {
      states.width = configs.width;
      states.height = configs.height;
    }
    states.time = 0.0f;
    states.wantUpdateMonitors = true;
    states.framebufferResized = false;
    states.title = configs.title;
    states.keyPressed.setOff();

    window = static_cast<GLFWwindow *>(
        zs::GlfwSystem::create_window(states.width, states.height /*, monitor*/, states.title));
#if 1
#  ifdef ZS_PLATFORM_WINDOWS
    // https://stackoverflow.com/questions/28524463/how-to-get-the-default-caption-bar-height-of-a-window-in-windows/28524464#28524464
    auto hWnd = glfwGetWin32Window(window);
    // const UINT dpi = GetDpiForWindow(myHwnd);
    int titleBarHeight = (GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION)
                          + GetSystemMetrics(SM_CXPADDEDBORDER));
    HMONITOR hMon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfo(hMon, &mi);
    glfwSetWindowSize(window, mi.rcWork.right - mi.rcWork.left,
                      mi.rcWork.bottom - mi.rcWork.top - titleBarHeight);
    glfwSetWindowPos(window, 0, titleBarHeight);
#  endif
#endif
    if (configs.fullscreen) glfwMaximizeWindow(window);

    {
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      states.dpiScale = (float)w / (float)states.width;
    }

    glfwGetWindowContentScale(window, &xscale, &yscale);
    states.scale = xscale > yscale ? xscale : yscale;
    // glfwGetFramebufferSize(window, &states.width, &states.height);

    //
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetInputMode(window, GLFW_STICKY_KEYS, true);

    // provide handle for callbacks
    glfwSetWindowUserPointer(window, &states);
    for (int i = 0; i != states.monitorCount; ++i)
      glfwSetMonitorUserPointer(glfw_monitors[i], &states);

    // cursors
    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
    g_MouseCursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#if GLFW_HAS_NEW_CURSORS
    g_MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
    g_MouseCursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_MouseCursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
    glfwSetErrorCallback(prev_error_callback);
#if GLFW_HAS_GETERROR && !defined(__EMSCRIPTEN__)  // Eat errors (see #5908)
    (void)glfwGetError(nullptr);
#endif

    setup_glfw_callbacks(window);  // mostly for imgui

    ///
    /// imgui
    ///
    zs::ImguiSystem::initialize();

    ImGuiIO &io = ImGui::GetIO();
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;      // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    zs::ImguiSystem::instance().setup(this);

    /// setup imgui platform monitors
    glfwSetMonitorCallback([](GLFWmonitor *monitor, int v) {
      auto &states = *static_cast<InteractiveStates *>(glfwGetMonitorUserPointer(monitor));

      if (states.wantUpdateMonitors) {
        states.wantUpdateMonitors = false;
        states.updateMonitors();
      }
      ///
      states.monitorCb(monitor, v);
    });
    states.updateMonitors();

    /// imgui viewport
    // Set platform dependent data in viewport
    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    mainViewport->PlatformHandle = (void *)window;
#if defined(ZS_PLATFORM_WINDOWS)
    mainViewport->PlatformHandleRaw = glfwGetWin32Window(window);
#elif defined(ZS_PLATFORM_OSX)
    mainViewport->PlatformHandleRaw = (void *)glfwGetCocoaWindow(window);
#else
    IM_UNUSED(mainViewport);
#endif

// Windows: register a WndProc hook so we can intercept some messages.
#ifdef ZS_PLATFORM_WINDOWS
    g_prevWndProc
        = (WNDPROC)::GetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC);
    IM_ASSERT(g_prevWndProc != nullptr);
    ::SetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC,
                       (LONG_PTR)imgui_wndproc);
#endif

    mainViewport->RendererUserData = IM_NEW(ImGuiVkRendererViewportData)();

    ///
    /// vulkan
    ///
    auto &ctx = Vulkan::context(0);
    states.pCtx = &ctx;
    states.swapchain
        = ctx.swapchain(createSurface()).setSamples(vk::SampleCountFlagBits::e4).build();
    auto &swapchain = states.swapchain.get();
    states.rp = swapchain.getRenderPass();
    auto &rp = states.rp.get();
    swapchain.initFramebuffersFor(rp);

    fmt::print(
        "swapchain color format: {}. depth/stencil format: {}. sample "
        "bits: {}\n",
        reflect_vk_enum(swapchain.getColorFormat()), reflect_vk_enum(swapchain.getDepthFormat()),
        reflect_vk_enum(swapchain.getSampleBits()));

    for (int i = 0; i != num_buffered_frames; ++i)
      states.cmds.push_back(
          ctx.createCommandBuffer(vk_cmd_usage_e::reset, vk_queue_e::graphics, false));

    states.renderer
        = ImguiVkRenderer{this, ctx, rp, swapchain.getSampleBits(), num_buffered_frames};

    states.sceneEditor = SceneEditor();

    //
    states.sceneEditor.get().setup(ctx, states.renderer.get());

    {
      /// init after imgui renderer init
      ResourceSystem::update_icon_texture(ctx, icon_e::play, "play-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::pause, "pause-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::stop, "stop-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::simulate, "simulate_star-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::speed_up, "speed_up-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::file, "file-line.png");
      ResourceSystem::update_icon_texture(ctx, icon_e::folder, "folder-line.png");
    }

    setupGUI();  // some widgets are from SceneEditor, must be setup later!

    ///
    zs_setup_world();

    auto uiModule = zs_world_get_module("ui");

    fmt::print("zpy.ui module at {}\n", uiModule);
    if (!(EditorGraph_Init(uiModule))) {
      fmt::print("successfully registered editorgraph class in ui.\n");
    } else
      fmt::print("failed to register editorgraph class in ui.\n");
  }

  GUIWindow::~GUIWindow() {
#if ZS_ENABLE_USD
    ResourceSystem::save_usd();
#endif
    /// vulkan
    states.ctx().device.waitIdle();

    states.cmds.clear();

    /// imgui
    ImGuiIO &io = ImGui::GetIO();
    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    if (ImGuiVkRendererViewportData *vd
        = (ImGuiVkRendererViewportData *)mainViewport->RendererUserData)
      IM_DELETE(vd);
    mainViewport->RendererUserData = nullptr;

    /// glfw
    for (ImGuiMouseCursor n = 0; n < ImGuiMouseCursor_COUNT; n++) {
      glfwDestroyCursor(g_MouseCursors[n]);
      g_MouseCursors[n] = NULL;
    }
#if defined(ZS_PLATFORM_WINDOWS)
    ::SetWindowLongPtr((HWND)mainViewport->PlatformHandleRaw, GWLP_WNDPROC,
                       (LONG_PTR)g_prevWndProc);
    g_prevWndProc = nullptr;
#endif

    ImGui::DestroyPlatformWindows();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags
        &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos
                         | ImGuiBackendFlags_HasGamepad | ImGuiBackendFlags_PlatformHasViewports
                         | ImGuiBackendFlags_HasMouseHoveredViewport);

    zs::GlfwSystem::destroy_window(window);
    g_activeWindow = nullptr;
  }

  void GUIWindow::reloadPlugins(const char *path) {
    // states.taskManager.reloadPlugins(path);
    auto *world = zs_get_world();
    auto *plugin = zs_get_plugin_manager(world);

    plugin->loadPluginsAt(path);

    plugin->displayNodeFactories();
  }

  /// glfw vulkan
  VkSurfaceKHR GUIWindow::createSurface() const {  // destroy on exit through callback
    VkSurfaceKHR surface;
    VkResult err = glfwCreateWindowSurface(Vulkan::vk_inst(), window, nullptr, &surface);
    if (err != VK_SUCCESS) throw std::runtime_error("failed to craete window surface");

    Vulkan::add_instance_destruction_callback([surface]() {
      Vulkan::vk_inst().destroySurfaceKHR(surface, nullptr, zs::Vulkan::vk_inst_dispatcher());
    });
    return surface;
  }

}  // namespace zs