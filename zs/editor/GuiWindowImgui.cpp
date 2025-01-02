#include <boost/process.hpp>
#include <chrono>

#include "GuiWindow.hpp"
#include "IconsMaterialDesign.h"
#include "ImGuiFileDialog.h"
#include "editor/widgets/AssetBrowserComponent.hpp"
#include "editor/widgets/DetailWidgetComponent.hpp"
#include "editor/widgets/TreeWidgetComponent.hpp"
#include "editor/widgets/WidgetComponent.hpp"
#include "editor/widgets/utilities/parser.hpp"
#include "imgui.h"
#include "interface/world/value_type/ValueInterface.hpp"
// #include "subprocess.h" // this is useless

#include "editor/widgets/ResourceWidgetComponent.hpp"
#include "editor/widgets/SequencerComponent.hpp"
#include "interface/details/PyHelper.hpp"
#include "widgets/WidgetEvent.hpp"
#include "world/system/ResourceSystem.hpp"

/// @note goto https://fonts.google.com/icons?icon.size=24&icon.color=%235f6368 for icon overview
#include "imgui.h"

namespace zs {

  namespace bp = boost::process;

  zs::StateMachine imgui_mouse_statemachine(GuiEventHub &eventQueue) {
    int phase = 0;
    std::array<ImVec2, 2> poses;
    std::array<double, 2> times;
    ImGuiMouseButton lastButton{-1};
    int accumClickTimes = 0;
    auto &io = ImGui::GetIO();

  released: {
    auto e_ = co_await zs::Event<MousePressEvent *>{};
    auto e = std::get<MousePressEvent *>(e_);
    if (e->source() != ImGuiMouseSource_Mouse) {
      goto released;  // only focus on mouse event
    }
    auto button = e->button();
    auto pos = e->windowPos();
    auto time = e->time();
    // check whether reset click progress
    if (button != lastButton && lastButton != -1) {
      phase = 0;  // new button clicking
    } else if (phase == 1) {
      int prevPhase = 0;
      if (time - times[prevPhase] > io.MouseDoubleClickTime) {
        phase = 0;
      } else if (ImVec2 delta = pos - poses[prevPhase];
                 ImLengthSqr(delta) > io.MouseDoubleClickMaxDist * io.MouseDoubleClickMaxDist) {
        phase = 0;
      }
    }

    lastButton = button;
    poses[phase] = pos;
    times[phase] = time;
    goto pressed;
  }
  pressed: {
    // [phase] already filled by press event
    auto e_ = co_await zs::Event<MouseReleaseEvent *>{};
    auto e = std::get<MouseReleaseEvent *>(e_);
    if (e->source() != ImGuiMouseSource_Mouse) goto pressed;  // only focus on mouse event
    auto button = e->button();
    if (button != lastButton) goto pressed;  // ignore this irrelevant event

    auto pos = e->windowPos();
    auto time = e->time();

    if (time - times[phase] > io.MouseDoubleClickTime) {
      phase = 0;
    } else if (ImVec2 delta = pos - poses[phase];
               ImLengthSqr(delta) > io.MouseDoubleClickMaxDist * io.MouseDoubleClickMaxDist) {
      phase = 0;
    } else
      phase++;
    if (phase == 2) {
      eventQueue.addEvent(
          new MouseDoubleClickEvent({poses[0], button, time, e->modifiers(), e->source()}));
      puts("DOUBLE CLICKED!");
      phase = 0;
    }
    goto released;
  }
  }

  void GUIWindow::setupGUI() {
    globalWidget = WindowWidgetNode(
        "Root", nullptr,
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    // rootWidget.setupMessageQueue();
    states._eventQueue.setupMessageQueue();

    // WindowWidgetNode &globalWidget = refGlobalWidget();
    WindowWidgetNode &globalWidget = this->globalWidget;

    globalWidget.setStyle(ImGuiStyleVar_WindowRounding, 0.0f);
    globalWidget.setStyle(ImGuiStyleVar_WindowBorderSize, 0.0f);
    globalWidget.setStyle(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    globalWidget.setTopLevel();

    // states.defaultGraph = zs::make_shared<ge::Graph>("Editor");
    states.graphs["Editor"] = make_shared<ge::Graph>("Editor");
    states.terminal = make_shared<Terminal>();
#if 0
    std::vector<ZsVar> sceneData;
    sceneData.emplace_back(zs_i64(13));
    sceneData.emplace_back(zs_i32(1));
    sceneData.emplace_back(zs_f64(3.3));
    {
      ZsList tmp = zs_list_obj_default();
      tmp.appendSteal(zs_long_obj_long_long(1));
      tmp.appendSteal(zs_long_obj_long_long(2));
      tmp.appendSteal(zs_long_obj_long_long(3));
      tmp.appendSteal(zs_long_obj_long_long(6));
      tmp.appendSteal(zs_long_obj_long_long(7));
      tmp.appendSteal(zs_long_obj_long_long(8));
      sceneData.emplace_back(tmp);
    }
    {
      ZsList tmp = zs_list_obj_default();
      tmp.appendSteal(zs_float_obj_double(0.1));
      tmp.appendSteal(zs_float_obj_double(0.2));
      sceneData.emplace_back(tmp);
    }
    sceneData.emplace_back(zs_bool_obj(true));
    sceneData.emplace_back(zs_bytearray_obj_cstr("wtf_bytearray"));
    sceneData.emplace_back(zs_bytes_obj_cstr("wtf_bytes"));
    sceneData.emplace_back(zs_string_obj_cstr("wtf_unicode"));
    {
      ZsBytes a0 = zs_bytes_obj_cstr("a0");
      a0.reflect();
      ZsObject a1 = zs_long_obj_double(123.11);
      a1.reflect();
      ZsList a2 = zs_list_obj_default();
      a2.reflect();
      // auto tup = zs_tuple_obj_pack_ptrs(3, a0.handle(), a1.handle(),
      // a2.handle());
      ZsVar tup = zs_tuple_obj_pack_zsobjs(3, a0, a1, a2);
      //
      // Py_BuildValue("(OO)", a0, a1);
      // ZsVar tup = PyTuple_Pack(3, a0, a1, a2);
      tup.reflect();
    }
    {
      ZsVar res = zs_eval_expr("[12, 331, '123123']");
      res.reflect();
      {
        int state;
        res = zs_execute_statement("bytearray(\'abc\')", &state);
        PyVar msg;
        res = zs_eval_expr("bytearray('abc')", (void **)msg.pHandle());
        // res = zs_eval_expr("b\'123123\'", (void **)msg.pHandle());
        if (msg.handle())
          fmt::print("err: {}\n", msg.asBytes().c_str());
        else
          res.reflect();
      }
      {
        PyVar msg;
        res = zs_eval_expr("[12, 331, '123123'", (void **)msg.pHandle());
        if (msg.handle()) fmt::print("err: {}\n", msg.asBytes().c_str());
      }
      {
        PyVar msg;
        res = zs_eval_expr("exit()", (void **)msg.pHandle());
        if (msg.handle()) fmt::print("err: {}\n", msg.asBytes().c_str());
      }
    }

    states.sceneData = zs::move(sceneData);
#endif

    globalWidget.layoutBuilder.embedBuildRoutine([this](DockingLayoutBuilder &builder) -> void {
      ImGuiID lu, ld, md, ru, rd, mu;
      builder
          .begin()
      /// @note always operate on newly-splited dock nodes or the root node!
#if 0
          .split(0.8f, widget_split_right, &ru)
          .split(ru, 0.3, widget_split_down, &rd)
          .split(ru, 0.5, widget_split_left, &mu)
          .split(rd, 0.5, widget_split_left, &md)
          .split(0.3f, widget_split_down, &ld)
#else
          .split(0.8f, widget_split_right, &ru)
          .split(ru, 0.7, widget_split_left, &mu)
          .split(ru, 0.3, widget_split_down, &rd)
          .split(mu, 0.3, widget_split_down, &md)
          .split(0.3f, widget_split_down, &ld)
#endif
          // graph editor
          .dockWidget((const char *)ICON_MD_SCHEMA u8"结点图", ru)
          .dockWidget((const char *)ICON_MD_SOURCE u8"文本编辑", ru)
          // terminal
          .dockWidget((const char *)ICON_MD_CODE u8"控制台", rd)
          // scene editor
          .dockWidget((const char *)ICON_MD_PREVIEW u8"场景视口", mu)
          // asset manager
          .dockWidget((const char *)ICON_MD_CATEGORY u8"资源管理", md)
          .dockWidget((const char *)ICON_MD_TIMELINE u8"时间轴", md)
          // primary panels
          .dockWidget("Dear ImGui Demo")
          .dockWidget((const char *)ICON_MD_SETTINGS u8"控制面板")
          .dockWidget((const char *)ICON_MD_ACCOUNT_TREE u8"USD树")
          .dockWidget((const char *)ICON_MD_ACCOUNT_TREE u8"场景树")
          // secondary panels
          .dockWidget((const char *)ICON_MD_TUNE u8"属性面板", ld)
          .build();
    });

    /// menu
    auto menuBarWidget = MenuBarWidgetComponent{};
    menuBarWidget.appendMenu((const char *)ICON_MD_MENU u8"文件");
    menuBarWidget.withMenu((const char *)ICON_MD_MENU u8"文件")
        .appendItemWithAction(
            (const char *)ICON_MD_FILE_OPEN u8"导入USD",
            [&dialogLabel = this->states.dialogLabel,
             &dialogCallback = this->states.dialogCallback]() {
              IGFD::FileDialogConfig config;
              config.path = ".";
              config.countSelectionMax = 1;
              config.flags = ImGuiFileDialogFlags_Modal;
              dialogLabel = (const char *)u8"打开对话框";
              ImGuiFileDialog::Instance()->OpenDialog((const char *)u8"打开对话框",
                                                      (const char *)u8"选择文件",
                                                      ".usd,.usda,.usdc", config);
              dialogCallback = [&dialogLabel]() {
                if (ImGuiFileDialog::Instance()->IsOk()) {  // action if OK
                  std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                  std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
          // action
          // fmt::print("selected {} at {}\n", filePathName, filePath);
#if ZS_ENABLE_USD
                  auto label = std::filesystem::path(filePathName).stem().string();
                  auto scene = ResourceSystem::load_usd(filePathName, /*label*/ label);
                  ResourceSystem::register_widget(
                      /*label*/ label, ui::build_usd_tree_node(scene->getRootPrim().get()));
#endif
                  dialogLabel = {};
                }
              };
            })
        .appendItemWithAction(
            (const char *)ICON_MD_SOURCE u8"导入脚本",
            [&dialogLabel = this->states.dialogLabel,
             &dialogCallback = this->states.dialogCallback]() {
              IGFD::FileDialogConfig config;
              config.path = ".";
              config.countSelectionMax = 1;
              config.flags = ImGuiFileDialogFlags_Modal;
              dialogLabel = (const char *)u8"脚本对话框";
              ImGuiFileDialog::Instance()->OpenDialog((const char *)u8"脚本对话框",
                                                      (const char *)u8"选择文件", ".py", config);
              dialogCallback = [&dialogLabel]() {
                if (ImGuiFileDialog::Instance()->IsOk()) {  // action if OK
                  std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                  std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                  // action
                  // fmt::print("selected {} at {}\n", filePathName, filePath);
                  ResourceSystem::load_script(filePathName, filePathName);
                  dialogLabel = {};
                }
              };
            })
        .appendItemWithAction(
            (const char *)ICON_MD_IMAGE u8"导入图像",
            [&dialogLabel = this->states.dialogLabel, &dialogCallback = this->states.dialogCallback,
             this]() {
              IGFD::FileDialogConfig config;
              config.path = ".";
              config.countSelectionMax = 1;
              config.flags = ImGuiFileDialogFlags_Modal;
              dialogLabel = (const char *)u8"图像对话框";
              ImGuiFileDialog::Instance()->OpenDialog((const char *)u8"图像对话框",
                                                      (const char *)u8"选择文件",
                                                      ".png,.jpg,.jpeg,.bmp", config);
              dialogCallback = [&dialogLabel, this]() {
                if (ImGuiFileDialog::Instance()->IsOk()) {  // action if OK
                  std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                  std::string filePath = ImGuiFileDialog::Instance()->GetCurrentPath();
                  // action
                  // fmt::print("selected {} at {}\n", filePathName, filePath);
                  zs_resources().load_texture(states.ctx(), filePathName,
                                              zs_resources().get_shader("imgui.frag"), 0, 0);
                  dialogLabel = {};
                }
              };
            })
        .appendMenu((const char *)ICON_MD_ADD u8"新建")
        .appendItemWithAction((const char *)ICON_MD_REFRESH u8"重新加载",
                              [&graphs = states.graphs]() {
                                auto &graph = graphs.at("Editor");
                                graph->load();
                              },
                              (const char *)u8"Ctrl+L")
        .appendItemWithAction((const char *)ICON_MD_SAVE u8"保存",
                              [&graphs = states.graphs]() {
                                auto &graph = graphs.at("Editor");
                                graph->save();
                              },
                              (const char *)u8"Ctrl+S")
        .appendItem((const char *)ICON_MD_SAVE_AS u8"保存为", (const char *)u8"Ctrl+Shift+S")
        .appendWidget(ActionWidgetComponent{[]() { ImGui::Separator(); }})
        .appendItem((const char *)ICON_MD_SETTINGS_INPUT_COMPONENT u8"设置")
        .appendWidget(ActionWidgetComponent{[]() { ImGui::Separator(); }})
        .appendItemWithAction((const char *)ICON_MD_CLOSE u8"退出",
                              []() { PyExecSystem::request_termination(); });

    menuBarWidget.withMenu((const char *)ICON_MD_MENU u8"文件")
        .withMenu((const char *)ICON_MD_ADD u8"新建")
        .appendItemWithAction((const char *)ICON_MD_SOURCE u8"脚本", []() {});
    menuBarWidget.withMenu((const char *)ICON_MD_MENU u8"文件")
        .withMenu((const char *)ICON_MD_ADD u8"新建")
        .appendItemWithAction((const char *)ICON_MD_NOTE_ADD u8"项目", []() {});
    menuBarWidget.withMenu((const char *)ICON_MD_MENU u8"文件")
        .withMenu((const char *)ICON_MD_ADD u8"新建")
        .appendItemWithAction((const char *)ICON_MD_SCHEMA u8"结点图", []() {});

    menuBarWidget.appendMenu((const char *)ICON_MD_SPACE_DASHBOARD u8"窗口");
    menuBarWidget.withMenu((const char *)ICON_MD_SPACE_DASHBOARD u8"窗口")
        .appendItemWithAction((const char *)u8"重置布局",
                              [this]() { refGlobalWidget().requireLayoutRebuild(); },
                              (const char *)u8"Alt+R");
    menuBarWidget.withMenu((const char *)ICON_MD_SPACE_DASHBOARD u8"窗口")
        .appendItemWithAction((const char *)u8"重置主题风格",
                              []() { ImguiSystem::reset_styles(); });

    menuBarWidget.appendMenu((const char *)u8"关于");
    menuBarWidget.withMenu((const char *)u8"关于").appendMenu((const char *)u8"信息");
    menuBarWidget.withMenu((const char *)u8"关于")
        .withMenu((const char *)u8"信息")
        .appendItem((const char *)u8"公司:泽森");
    menuBarWidget.withMenu((const char *)u8"关于")
        .withMenu((const char *)u8"信息")
        .appendItem((const char *)u8"作者:地雷");

    globalWidget.setMenuComponent(move(menuBarWidget));

    /// dialogs
    globalWidget.appendComponent(ActionWidgetComponent([this] {
      if (states.dialogLabel) {
        if (ImGuiFileDialog::Instance()->Display(*states.dialogLabel)) {
          if (states.dialogCallback) {
            states.dialogCallback();
          }
          // close
          ImGuiFileDialog::Instance()->Close();
        }
      }
    }));

    /// asset manager
    ResourceSystem::register_widget(g_defaultWidgetLabelAssetBrowser, new AssetBrowserComponent());
    auto assetManager
        = WindowWidgetNode{(const char *)ICON_MD_CATEGORY u8"资源管理", &globalWidget};
    assetManager.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelAssetBrowser));
    globalWidget.appendChild(move(assetManager));

    /// timeline
    ResourceSystem::register_widget(g_defaultWidgetLabelSequencer, new SequencerWidget());
    auto timelineManager
        = WindowWidgetNode{(const char *)ICON_MD_TIMELINE u8"时间轴", &globalWidget};
    timelineManager.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelSequencer));
    globalWidget.appendChild(move(timelineManager));

    /// text editor
    auto textEditor = WindowWidgetNode{(const char *)ICON_MD_SOURCE u8"文本编辑", &globalWidget};
    textEditor.appendComponent(new TextEditor());
    globalWidget.appendChild(move(textEditor));

    /// graph editor
    auto graphEditor = WindowWidgetNode{(const char *)ICON_MD_SCHEMA u8"结点图", &globalWidget};
    ResourceSystem::register_widget(g_defaultWidgetLabelGraphEditor,
                                    GraphWidgetComponent{states.graphs.at("Editor")});
    graphEditor.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelGraphEditor));
    globalWidget.appendChild(move(graphEditor));

    /// 3d scene viewport
    auto sceneView
        = WindowWidgetNode{(const char *)ICON_MD_PREVIEW u8"场景视口", &globalWidget,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_NoCollapse};
    ResourceSystem::register_widget(g_defaultWidgetLabelScene,
                                    states.sceneEditor.get().getWidget());
    sceneView.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelScene));
    globalWidget.appendChild(move(sceneView));

    /// terminal (placeholder)
    auto terminalWidget = WindowWidgetNode{(const char *)(ICON_MD_CODE u8"控制台"), &globalWidget};
    terminalWidget.appendComponent(
        TerminalWidgetComponent{(const char *)ICON_MD_CODE u8"控制台", states.terminal});
    globalWidget.appendChild(move(terminalWidget));

    /// control panel
    auto controlWidget
        = WindowWidgetNode{(const char *)ICON_MD_SETTINGS u8"控制面板", &globalWidget};
    controlWidget.appendComponent(ButtonWidgetComponent{
        "Reset Docking Layout", [this]() { refGlobalWidget().requireLayoutRebuild(); }});
    controlWidget.appendComponent(ButtonWidgetComponent{(const char *)u8"重置主题风格",
                                                        []() { ImguiSystem::reset_styles(); }});
    controlWidget.appendComponent(
        SliderWidgetComponent{"Global UI Scale", &ImGui::GetIO().FontGlobalScale, 0.005f, 0.3f,
                              2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp});
    controlWidget.appendComponent(ActionWidgetComponent{[this] {
      ImGui::Text("selected %d points.", (int)states.sceneEditor.get().selectedIndices.size());
    }});
    // camera
    controlWidget.appendComponent(states.sceneEditor.get().getCameraWidget());
///
#if 0
    controlWidget.appendComponent(ActionWidgetComponent([&sceneData = states.sceneData] {
      static std::vector<ui::GenericResourceWidget> widgets;
      static ZsVar vals[3];
      if (widgets.size() != sceneData.size() + 3) {
        widgets.clear();
        int i = 0;
        for (auto &res : sceneData) {
          widgets.emplace_back(fmt::format("{}", i++), res);
        }
        vals[0] = zs_long_obj_long_long(123);
        widgets.emplace_back(fmt::format("{}", i++), vals[0], zs_eval_expr("['asdf', 123, b'as']"));

        vals[1] = zs_string_obj_cstr("as");
        widgets.emplace_back(fmt::format("{}", i++), vals[1], zs_eval_expr("(b'asdf', 123, 'as')"));

        vals[2] = zs_long_obj_long_long(1);
        widgets.emplace_back(fmt::format("{}", i++), vals[2]);
      }

      for (auto &w : widgets) {
        if (auto width = w.preferredWidth(); width != 0.f)
          ImGui::SetNextItemWidth(width + ImGui::GetStyle().ItemInnerSpacing.x);
        w.draw();
      }
    }));
#endif
    ///
    globalWidget.appendChild(move(controlWidget));

    auto usdTreeWidget
        = WindowWidgetNode{(const char *)ICON_MD_ACCOUNT_TREE u8"USD树", &globalWidget};
    usdTreeWidget.appendComponent(new ui::SceneFileEditor());

    ResourceSystem::register_widget(g_defaultWidgetLabelSceneHierarchy, new ui::PrimitiveEditor());
    auto primTreeWidget
        = WindowWidgetNode{(const char *)ICON_MD_ACCOUNT_TREE u8"场景树", &globalWidget};
    primTreeWidget.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelSceneHierarchy));
    /*
    ActionWidgetComponent{[idx = -1, key = std::string(g_defaultUsdLabel)]() mutable {
          // combo
          const auto &usdKeys = ResourceSystem::get_usd_labels();
          std::vector<const char *> items(usdKeys.size());
          for (int i = 0; i < usdKeys.size(); ++i) {
            items[i] = usdKeys[i].data();
          }
          idx = -1;
          for (int i = 0; i < usdKeys.size(); ++i) {
            if (usdKeys[i] == key) {
              idx = i;
              break;
            }
          }
          ImGuiComboFlags flags = ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_WidthFitPreview;
          if (ImGui::BeginCombo("##usd_selection", idx != -1 ? items[idx] : "<null>", flags)) {
            for (int i = 0; i < usdKeys.size(); ++i) {
              const bool selected = idx;
              if (ImGui::Selectable(items[i], selected)) {
                idx = i;
                key = usdKeys[i];
              }
              if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
          // tree
          if (idx >= 0 && idx < items.size()) {
            ResourceSystem::get_widget_ptr(usdKeys[idx])->paint();
          }
        }}*/
    globalWidget.appendChild(move(usdTreeWidget));
    globalWidget.appendChild(move(primTreeWidget));

    /// (widget) detail panel (formerly known as property panel)
    auto propertyWidget = WindowWidgetNode{(const char *)ICON_MD_TUNE u8"属性面板", &globalWidget};

    ResourceSystem::register_widget(g_defaultWidgetLabelDetail, new DetailsWidgetComponent());
    propertyWidget.appendComponent(ResourceSystem::ref_widget(g_defaultWidgetLabelDetail));
#if 0
    propertyWidget.appendComponent(TextWidgetComponent{
        "Selected %llu objects", &states.graphs.at("Editor")->numSelectedNodes});

    propertyWidget.appendComponent(ActionWidgetComponent{[&graphs = states.graphs]() {
      ImGui::Spacing();
      auto &graph = graphs.at("Editor");
      if (auto ctx = graph->getEditorContext()) {
        if (auto currentAction = ctx->GetCurrentAction()) {
          ImGui::Text("current action: %s", currentAction->GetName());
          ;
        }
      }
    }});

    propertyWidget.appendComponent(ButtonWidgetComponent{
        "增加pin", [&graphs = states.graphs]() {
          auto &graph = graphs.at("Editor");
          if (graph->numSelectedNodes == 1) {
            auto node = graph->selectedNodes[0];
            if (auto pin = node->inputPin(0)) {
              pin->append(pin->_name + std::to_string(pin->_id.Get() + pin->_chs.size()));
            }
          }
        }});
    propertyWidget.appendComponent(ActionWidgetComponent{[&graphs = states.graphs]() {
      ImGui::Spacing();
      auto &graph = graphs.at("Editor");
      if (graph->numSelectedNodes == 1) {
        auto node = graph->selectedNodes[0];
        auto &text = node->cmdText;
        bool triggered = ImGui::InputTextWithHint(
            (const char *)u8"指令", (const char *)u8"输入指令", text.data(), text.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

        auto &resultText = node->helperText;
        if (ImGui::Button((const char *)u8"分析选项") || triggered) {
          resultText.clear();
          bp::ipstream is;  // reading pipe-stream

          if (!text.empty() && text[0] != '\0') {
            auto cmd = separate_string_by(text, " ;-\0")[0];
            try {
              bp::child c(bp::search_path(cmd), "--help", (bp::std_out & bp::std_err) > is);
              bool done = c.wait_for(std::chrono::milliseconds(500));

              if (done) {
                std::string line;
                while (std::getline(is, line))  // && !line.empty())
                  resultText += line + "\n";

                Lexer lexer(&resultText);
                lexer.parse();
                // lexer.printDict();

                node->clearInputs();
                for (auto &it : lexer.dict) {
                  if (!it[0].empty())
                    node->appendInput(it[0]);
                  else
                    node->appendInput(it[1]);
                }
              }

              // fmt::print("\n## READ ##\n {}\n", resultText);
            } catch (const bp::process_error &er) {
              ;
            }
          }
        }
        if (!resultText.empty()) ImGui::SeparatorText((const char *)u8"帮助文本");
        auto wrapWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapWidth);
        ImGui::TextUnformatted(resultText.c_str(), resultText.c_str() + resultText.size());
        ImGui::PopTextWrapPos();

        /// draw attribute items
        node->drawAttribItems();
      }
    }});
#endif
    globalWidget.appendChild(move(propertyWidget));

    states._mouseState = zs::move(imgui_mouse_statemachine(states._eventQueue));
  }

  void GUIWindow::drawGUI() {
    globalWidget.paint();
    ImGui::ShowDemoWindow();
  }

  void GUIWindow::processRemainingEvents(const std::vector<GuiEvent *> &evs) {
    if (evs.size()) {
      for (auto e : evs) {
        switch (e->getType()) {
          // mouse
          case gui_event_mousePressed: {
            auto e_ = dynamic_cast<MousePressEvent *>(e);
            states._mouseState.onEvent(e_);
            delete e_;
            break;
          }
          case gui_event_mouseReleased: {
            auto e_ = dynamic_cast<MouseReleaseEvent *>(e);
            states._mouseState.onEvent(e_);
            delete e_;
            break;
          }
          case gui_event_mouseMoved: {
            auto e_ = dynamic_cast<MouseMoveEvent *>(e);
            states._mouseState.onEvent(e_);
            delete e_;
            break;
          }
          //
          default:
            break;
        }
      }
    }
  }

  void GUIWindow::generateImguiGuiEvents() {
    static u64 cnt = 0;
    auto &io = ImGui::GetIO();
    // mouse
    for (int m = 0; m < ImGuiMouseButton_COUNT; m++) {
      // 0: ImGuiMouseButton_Left, 1: ImGuiMouseButton_Right, 2: ImGuiMouseButton_Middle
      // IsMouse(Double)Clicked is not performing as intended
      // https://github.com/ocornut/imgui/issues/2385
      if (ImGui::IsKeyPressed(ImGui::MouseButtonToKey(m), false)) {
        // fmt::print("[{}]\timgui mouse: PRESSED!\n", cnt);
        states._eventQueue.addEvent(
            new MousePressEvent{{io.MousePos,
                                 m,
                                 states.time,
                                 {.ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl),
                                  .shift = ImGui::IsKeyDown(ImGuiKey_LeftShift),
                                  .alt = ImGui::IsKeyDown(ImGuiKey_LeftAlt),
                                  .super = ImGui::IsKeyDown(ImGuiKey_LeftSuper)},
                                 ImGuiMouseSource_Mouse}});
      } else if (ImGui::IsMouseReleased(m)) {
        states._eventQueue.addEvent(
            new MouseReleaseEvent{{io.MousePos,
                                   m,
                                   states.time,
                                   {.ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl),
                                    .shift = ImGui::IsKeyDown(ImGuiKey_LeftShift),
                                    .alt = ImGui::IsKeyDown(ImGuiKey_LeftAlt),
                                    .super = ImGui::IsKeyDown(ImGuiKey_LeftSuper)},
                                   ImGuiMouseSource_Mouse}});
        // fmt::print("[{}]\timgui mouse: RELEASED!\n", cnt);
      }
    }
    if (io.MouseDelta[0] != 0.f || io.MouseDelta[1] != 0.f) {
      // fmt::print("[{}]\timgui mouse: MOVING ({}, {})!\n", cnt, io.MouseDelta[0],
      // io.MouseDelta[1]);
    }
    // key
    for (int k_ = ImGuiKey_Keyboard_BEGIN; k_ < ImGuiKey_Keyboard_END; k_++) {
      ImGuiKey k = (ImGuiKey)k_;
      if (!ImGui::IsNamedKeyOrMod(k)) continue;
      if (ImGui::IsKeyPressed(k, false)) {
        ;
      }
    }
    cnt++;
  }

}  // namespace zs