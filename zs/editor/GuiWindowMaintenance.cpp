#include "GlfwSystem.hpp"
#include "GuiWindow.hpp"
#include "imgui.h"
//
#include "ImGuizmo.h"
#include "ImguiSystem.hpp"

namespace zs {

/// glfw
void GUIWindow::maximizeWindow() { glfwMaximizeWindow(window); }
void GUIWindow::minimizeWindow() { glfwIconifyWindow(window); }

void GUIWindow::InteractiveStates::updateMonitors() {
  ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
  GLFWmonitor **glfwMonitors = glfwGetMonitors(&this->monitorCount);
  if (monitorCount == 0)
    return;

  platform_io.Monitors.resize(0);
  for (int n = 0; n < monitorCount; n++) {
    ImGuiPlatformMonitor monitor;
    int x, y;
    glfwGetMonitorPos(glfwMonitors[n], &x, &y);
    const GLFWvidmode *vidMode = glfwGetVideoMode(glfwMonitors[n]);
    if (vidMode == nullptr)
      continue; // Failed to get Video mode (e.g. Emscripten does not
                // support this function)
    monitor.MainPos = monitor.WorkPos = ImVec2((float)x, (float)y);
    monitor.MainSize = monitor.WorkSize =
        ImVec2((float)vidMode->width, (float)vidMode->height);
#if GLFW_HAS_MONITOR_WORK_AREA
    int w, h;
    glfwGetMonitorWorkarea(glfwMonitors[n], &x, &y, &w, &h);
    if (w > 0 &&
        h > 0) // Workaround a small GLFW issue reporting zero on monitor
               // changes: https://github.com/glfw/glfw/pull/1761
    {
      monitor.WorkPos = ImVec2((float)x, (float)y);
      monitor.WorkSize = ImVec2((float)w, (float)h);
    }
#endif
#if GLFW_HAS_PER_MONITOR_DPI
    // Warning: the validity of monitor DPI information on Windows depends
    // on the application DPI awareness settings, which generally needs to
    // be set in the manifest or at runtime.
    float x_scale, y_scale;
    glfwGetMonitorContentScale(glfwMonitors[n], &x_scale, &y_scale);
    monitor.DpiScale = x_scale;
#endif
    monitor.PlatformHandle = (void *)
        glfwMonitors[n]; // [...] GLFW doc states: "guaranteed to be valid
                         // only until the monitor configuration changes"
    platform_io.Monitors.push_back(monitor);
  }
}

void GUIWindow::updateImguiMouseData() {
  ImGuiIO &io = ImGui::GetIO();
  ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();

  ImGuiID mouse_viewport_id = 0;
  const ImVec2 mouse_pos_prev = io.MousePos;
  for (int n = 0; n < platform_io.Viewports.Size; n++) {
    ImGuiViewport *viewport = platform_io.Viewports[n];
    GLFWwindow *window = (GLFWwindow *)viewport->PlatformHandle;

    const bool is_window_focused =
        glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
    if (is_window_focused) {
      // (Optional) Set OS mouse position from Dear ImGui if requested (rarely
      // used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by
      // user) When multi-viewports are enabled, all Dear ImGui positions are
      // same as OS positions.
      if (io.WantSetMousePos)
        glfwSetCursorPos(window, (double)(mouse_pos_prev.x - viewport->Pos.x),
                         (double)(mouse_pos_prev.y - viewport->Pos.y));

      // (Optional) Fallback to provide mouse position when focused
      // (ImGui_ImplGlfw_CursorPosCallback already provides this when hovered or
      // captured)
      if (states.mouseWindow == nullptr) {
        double mouse_x, mouse_y;
        glfwGetCursorPos(window, &mouse_x, &mouse_y);
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
          // Single viewport mode: mouse position in client window coordinates
          // (io.MousePos is (0,0) when the mouse is on the upper-left corner of
          // the app window) Multi-viewport mode: mouse position in OS absolute
          // coordinates (io.MousePos is (0,0) when the mouse is on the
          // upper-left of the primary monitor)
          int window_x, window_y;
          glfwGetWindowPos(window, &window_x, &window_y);
          mouse_x += window_x;
          mouse_y += window_y;
        }
        states.lastValidMousePos =
            zs::vec<float, 2>((float)mouse_x, (float)mouse_y);
        io.AddMousePosEvent((float)mouse_x, (float)mouse_y);
      }
    }

    // (Optional) When using multiple viewports: call io.AddMouseViewportEvent()
    // with the viewport the OS mouse cursor is hovering. If
    // ImGuiBackendFlags_HasMouseHoveredViewport is not set by the backend, Dear
    // imGui will ignore this field and infer the information using its flawed
    // heuristic.
    // - [X] GLFW >= 3.3 backend ON WINDOWS ONLY does correctly ignore viewports
    // with the _NoInputs flag.
    // - [!] GLFW <= 3.2 backend CANNOT correctly ignore viewports with the
    // _NoInputs flag, and CANNOT reported Hovered Viewport because of mouse
    // capture.
    //       Some backend are not able to handle that correctly. If a backend
    //       report an hovered viewport that has the _NoInputs flag (e.g. when
    //       dragging a window for docking, the viewport has the _NoInputs flag
    //       in order to allow us to find the viewport under), then Dear ImGui
    //       is forced to ignore the value reported by the backend, and use its
    //       flawed heuristic to guess the viewport behind.
    // - [X] GLFW backend correctly reports this regardless of another viewport
    // behind focused and dragged from (we need this to find a useful drag and
    // drop target).
    // FIXME: This is currently only correct on Win32. See what we do below with
    // the WM_NCHITTEST, missing an equivalent for other systems. See
    // https://github.com/glfw/glfw/issues/1236 if you want to help in making
    // this a GLFW feature.
#if GLFW_HAS_MOUSE_PASSTHROUGH || (GLFW_HAS_WINDOW_HOVERED && defined(_WIN32))
    const bool window_no_input =
        (viewport->Flags & ImGuiViewportFlags_NoInputs) != 0;
#if GLFW_HAS_MOUSE_PASSTHROUGH
    glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, window_no_input);
#endif
    if (glfwGetWindowAttrib(window, GLFW_HOVERED) && !window_no_input)
      mouse_viewport_id = viewport->ID;
#else
    // We cannot use bd->MouseWindow maintained from CursorEnter/Leave
    // callbacks, because it is locked to the window capturing mouse.
#endif
  }

  if (io.BackendFlags & ImGuiBackendFlags_HasMouseHoveredViewport)
    io.AddMouseViewportEvent(mouse_viewport_id);
}
void GUIWindow::updateImguiMouseCursor() {
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) ||
      glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    return;

  ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();

  ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
  for (int n = 0; n < platform_io.Viewports.Size; n++) {
    GLFWwindow *window = (GLFWwindow *)platform_io.Viewports[n]->PlatformHandle;
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) {
      // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    } else {
      // Show OS mouse cursor
      // FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse
      // cursor with GLFW 3.2, but 3.3 works here.
      glfwSetCursor(window,
                    get_imgui_mouse_cursor(imgui_cursor)
                        ? get_imgui_mouse_cursor(imgui_cursor)
                        : get_imgui_mouse_cursor(ImGuiMouseCursor_Arrow));
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
  }
}

bool GUIWindow::beginFrame() {
  auto &swapchain = states.swapchain.get();
  auto res = swapchain.acquireNextImage(states.curImageId);
  if (res == vk::Result::eErrorOutOfDateKHR || wasWindowResized()) {
    resetWindowResizedFlag();

    auto [w, h] = getExtent();

    auto &ctx = states.ctx();
    if (w > 0 && h > 0) {
      ctx.device.waitIdle(ctx.dispatcher);
      ctx.swapchain().resize(swapchain, w, h);
      swapchain.initFramebuffersFor(states.rp.get());
    }
    return false;
  }

  ///
  /// update imgui resources
  ///
  int bufferNo = swapchain.getCurrentFrame();

  ImGuiIO &io = ImGui::GetIO();
  // Setup display size (every frame to accommodate for window resizing)
  int w, h;
  int display_w, display_h;
  glfwGetWindowSize(window, &w, &h);
  glfwGetFramebufferSize(window, &display_w, &display_h);
  io.DisplaySize = ImVec2((float)w, (float)h);
  if (w > 0 && h > 0)
    io.DisplayFramebufferScale =
        ImVec2((float)display_w / (float)w, (float)display_h / (float)h);
  if (states.wantUpdateMonitors)
    states.updateMonitors();

  // Setup time step
  // (Accept glfwGetTime() not returning a monotonically increasing value. Seems
  // to happens on disconnecting peripherals and probably on VMs and Emscripten,
  // see #6491, #6189, #6114, #3644)
  double current_time = glfwGetTime();
  if (current_time <= states.time)
    current_time = states.time + 0.00001f;
  io.DeltaTime = states.time > 0.0 ? (float)(current_time - states.time)
                                   : (float)(1.0f / 60.0f);
  states.time = current_time;

  updateImguiMouseData();
  updateImguiMouseCursor();

  states.sceneEditor.get().update(io.DeltaTime);

  ImGui::NewFrame();

  /// @brief custom gui event loop!
  // generate and post imgui events
  generateImguiGuiEvents();
  std::vector<GuiEvent *> unhandled;
  states._eventQueue.postEvents(&globalWidget, &unhandled);
  processRemainingEvents(unhandled);
  /// @note more complex events might be newly setup
  states._eventQueue.postEvents(&globalWidget, nullptr);

  ImGui::PushFont((ImFont *)ImguiSystem::get_font(ImguiSystem::cn_font));
  ImGuizmo::BeginFrame();

  drawGUI();

  ImGui::PopFont();
  ImGui::Render(); // thus no need to call EndFrame

  states.renderer.get().updateBuffers(bufferNo);

  auto &cmd = currentCmd();
  cmd.begin();

  return true;
}

void GUIWindow::tick() {
  auto &swapchain = states.swapchain.get();
  // scene render pass
  if (states.renderer.get().viewportRequireSceneRenderResults())
    states.sceneEditor.get().renderFrame(swapchain.getCurrentFrame(), currentCmd());
}

void GUIWindow::beginRender() {
  auto &swapchain = states.swapchain.get();
  auto &cmd = currentCmd();
  auto renderAreaExtent = swapchain.getExtent();
  vk::Rect2D rect = vk::Rect2D(vk::Offset2D(), renderAreaExtent);
  auto clearValues = swapchain.getClearValues();

  auto renderPassInfo =
      vk::RenderPassBeginInfo()
          .setRenderPass(states.rp.get())
          .setFramebuffer(swapchain.frameBuffer(currentImageId()))
          .setRenderArea(rect)
          .setClearValueCount((zs::u32)clearValues.size())
          .setPClearValues(clearValues.data());
  (*cmd).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
}
void GUIWindow::renderFrame() {
  int bufferNo = states.swapchain.get().getCurrentFrame();
  auto &cmd = currentCmd();
  states.renderer.get().renderFrame(bufferNo, cmd);
}
void GUIWindow::endRender() {
  auto &cmd = currentCmd();
  (*cmd).endRenderPass();
}

void GUIWindow::endFrame() {
  auto &ctx = states.ctx();
  auto &swapchain = states.swapchain.get();
  auto &cmd = currentCmd();
  auto imgId = currentImageId();
  auto queue = cmd.getQueue();

  (*cmd).end();

  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    /// @ref
    /// https://github.com/ocornut/imgui/wiki/Getting-Started#additional-code-to-enable-multi-viewports
    // Update and Render additional Platform Windows
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

  bool resized = false;
  {
    vk::Result res;
    if ((VkFence)swapchain.imageFence(imgId) != VK_NULL_HANDLE) {
      res = ctx.device.waitForFences({swapchain.imageFence(imgId)}, VK_TRUE,
                                     detail::deduce_numeric_max<u64>(),
                                     ctx.dispatcher);
    }
    swapchain.imageFence(imgId) = swapchain.currentFence();

    vk::PipelineStageFlags stages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput};
    cmd.waitStage(stages);
    cmd.wait(swapchain.currentImageAcquiredSemaphore());
    cmd.signal(swapchain.currentRenderCompleteSemaphore());
    cmd.submit(swapchain.currentFence(), /*reset fence*/ true,
               /*reset config*/ true);

    res = swapchain.present(queue, imgId);

    /// @note credits
    /// https://www.reddit.com/r/vulkan/comments/cc3edr/swapchain_recreation_repeatedly_returns_vk_error/
    if (res == vk::Result::eErrorOutOfDateKHR ||
        res == vk::Result::eSuboptimalKHR || wasWindowResized()) {
      resetWindowResizedFlag();

      auto [w, h] = getExtent();

      if (w > 0 && h > 0) {
        ctx.device.waitIdle(ctx.dispatcher);
        ctx.swapchain().resize(swapchain, w, h);
        swapchain.initFramebuffersFor(states.rp.get());
      }
      resized = true;
    }
  }

  if (!resized)
    swapchain.nextFrame();
}

} // namespace zs