#include "TextEditorComponent.hpp"

#include "WidgetComponent.hpp"
#include "WidgetDrawUtilities.hpp"
//
#include <float.h>

// #include "IconsFontAwesome6.h"
#include "world/core/Utils.hpp"
#include "IconsMaterialDesign.h"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "interface/details/Py.hpp"
#include "interface/details/PyHelper.hpp"
#include "editor/widgets/WidgetDrawUtilities.hpp"
#include "world/system/PyExecSystem.hpp"
#include "world/system/ResourceSystem.hpp"

namespace zs {

  ///
  TextEditor::TextEditor() {
    /// setup tab maintenance ops
    _tabs.setLeadingSymbolLiteral((const char *)ICON_MD_LIST);
    _tabs.setInsertionCallback([](std::string_view label, std::string &buffer) {
      buffer = ResourceSystem::get_script(std::string{label});
    });
    _tabs.setRemovalCallback([](std::string_view label, std::string &buffer) {
      if (label == g_textEditorLabel) {  // default text file is always saved upon removal
        ResourceSystem::update_script(g_textEditorLabel, buffer);
        ResourceSystem::save_script(g_textEditorLabel);
      }
    });
    _tabs.setIsValidPredicate([](std::string_view label) -> bool {
      return ResourceSystem::has_script(std::string{label});
    });
    _tabs.setIsModifiedPredicate([](std::string_view label) -> bool {
      return ResourceSystem::script_modified(std::string{label});
    });
    _tabs.setSaveCallback(
        [](std::string_view label) -> void { ResourceSystem::save_script(std::string{label}); });
    _tabs.setItemListCallback(
        []() -> std::vector<std::string> { return ResourceSystem::get_script_labels(); });
    //
    _tabs.appendTab(g_textEditorLabel);
  }
  TextEditor::~TextEditor() {
    // ResourceSystem::update_script(g_textEditorLabel, _buffer);
    // ResourceSystem::save_script(g_textEditorLabel);
  }

  void TextEditor::paint() {
    _tabs.paint();
    int selectedIdx = _tabs.getFocusId();

    ///
    /// update script if needed
    auto pCurBuffer = &_buffer;
    if (selectedIdx != -1 && _tabs.size() > 0) {
      auto &entry = _tabs[selectedIdx];
      pCurBuffer = &entry.content();
      if (ResourceSystem::script_modified(entry._label)) {
        *pCurBuffer = ResourceSystem::get_script(entry._label);
        // ResourceSystem::mark_script_clean(doc._label);
      }
    }

    auto &buffer = *pCurBuffer;
    auto &pyExec = PyExecSystem::instance();
    bool inProgress = pyExec.inProgress();
    // icon_e ie = icon_e::play;
    // ImVec4 bg_col = ImVec4(0.0f, 0.75f, 0.0f, 1.0f);
    if (inProgress || selectedIdx == -1) {
      ImGui::BeginDisabled(true);

      // ie = icon_e::stop;
      // bg_col = ImVec4(0.2f, 0.15f, 0.0f, 1.0f);
    }
#if 0
    ImVec2 size = ImVec2(32.0f, 32.0f);
    VkTexture &tex = ResourceSystem::get_icon_texture(ie);
    ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // No tint
    vk::Extent3D dim = tex.image.get().getExtent();
    if (ImGui::ImageButton("Run", (ImTextureID)(&ResourceSystem::get_icon_descr_set(ie)), size,
                           ImVec2(0.0f, 0.0f), ImVec2(size.x / dim.width, size.y / dim.height),
                           bg_col, tint_col)) {
    }
#endif
    if (!inProgress) {
      if (ImGui::Button((const char *)ICON_MD_PLAY_CIRCLE_OUTLINE u8"执行")) {
        bool res = pyExec.assignTask([this, &buffer] {
          ResourceSystem::start_cstream_capture();
          int result_ = 0;
          ZsValue ret = zs_execute_script(buffer.c_str(), &result_);
          ConsoleRecord::type_e result = result_ == 0 ? ConsoleRecord::info : ConsoleRecord::error;
          ResourceSystem::dump_cstream_capture();

          PyGILState_STATE gstate = PyGILState_Ensure();
          if (ret.isObject()) {
            assert(ret.isBytes());
            PyVar resultPyBytes = ret;
            if (resultPyBytes) {
              auto cstr = resultPyBytes.asBytes().c_str();
              ResourceSystem::push_assembled_log(cstr, result);
            } else {
              ResourceSystem::push_assembled_log("", result);
            }

            // if (result_ == 5) PyExecSystem::request_termination();
          }
          PyGILState_Release(gstate);
        });
      }
    } else {
      ImGui::Button((const char *)ICON_MD_STOP u8"停止");  // disabled
      // auto res = pyExec.tryFinish();
      // {
      //   if (_requestExit)
      //     PyExecSystem::request_termination();
      // }
    }

    if (inProgress || selectedIdx == -1) {
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (selectedIdx != -1)
      ImGui::Text((const char *)u8"正在编辑: %s", _tabs[selectedIdx].getLabel().data());
    else
      ImGui::Text((const char *)u8"空");

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, g_darkest_color);

    if (selectedIdx != -1)
      ImGui::PushID(ImGui::GetCurrentWindow()->GetID(_tabs[selectedIdx].getLabel().data()));

    // auto tag = fmt::format("##{}\n", _openedDocs[_selectedDocIdx]._label);
    if (ImGui::InputTextMultiline("##source", &buffer,
                                  ImVec2(-FLT_MIN, /*ImGui::GetTextLineHeight() * 16*/ -FLT_MIN),
                                  flags)) {
      if (selectedIdx != -1) {
        ResourceSystem::update_script(_tabs[selectedIdx].getLabel(), buffer);
      }
    }
    if (selectedIdx != -1) ImGui::PopID();

    ImGui::PopStyleColor();

    ///
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSETS_BROWSER_ITEMS")) {
        auto assets = (AssetEntry *)payload->Data;
        const auto numAssets = payload->DataSize / sizeof(AssetEntry);

        for (int i = 0; i < numAssets; ++i) {
          const auto &entry = assets[i];
          if (entry._type != AssetEntry::text_) continue;
          const auto &l = entry.getLabel();
          int idx = _tabs.tabOpened(l);
          // fmt::print("DROPPING asset file: {} to text editor!\n", assets[i].getLabel());

          if (idx != -1) {
            _tabs[idx]._visible = true;
          } else if (!l.empty()) {
            _tabs.appendTab(l);
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
  }

}  // namespace zs