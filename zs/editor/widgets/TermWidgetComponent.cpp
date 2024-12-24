#include "TermWidgetComponent.hpp"

#include "WidgetDrawUtilities.hpp"
//
#include <Python.h>

#include <sstream>

#include "IconsMaterialDesign.h"
#include "editor/GuiWindow.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "interface/details/Py.hpp"
#include "interface/world/ObjectInterface.hpp"
#include "utf8.h"
#include "world/core/Utils.hpp"
#include "world/system/PyExecSystem.hpp"

namespace zs {

  Terminal::Terminal()
      : _inputBuf(),
        _textSelect{[this](size_t idx) -> std::string_view { return _arrangedItems[idx]; },
                    [this]() -> size_t { return _arrangedItems.size(); }},
        _historyLimit{128},
        _historyPos{-1},
        _scrollToBottom{false},
        _focusInput{true},
        _requestExit{false} {
    _arrangedItems.reserve(ResourceSystem::get_log_entry_limit());
    _inputBuf.reserve(256);
    _commands.emplace_back("help", "display terminal command helper info.");
    _commands.emplace_back("hello", "hello.");
    addLog("Python Console\n");
  }
  Terminal::~Terminal() {}

  void Terminal::executeCommand(std::string_view cmd) {
    std::string cmdStr(cmd);
    _historyPos = -1;
    if (auto it = std::find(std::begin(_history), std::end(_history), cmdStr);
        it != _history.end()) {
      _history.erase(it);
    }

    _history.push_back(cmdStr);
    if (_history.size() >= _historyLimit) _history.pop_front();
    _scrollToBottom = true;
    ///
    if (cmd.size() > 1 && cmd[0] == '!' && cmd[1] != '!') {
      addLog(fmt::format("# {}\n", _inputBuf.c_str()));
      addLog(fmt::format("unknown command {}\n", cmd));
    } else {
      if (zs_world_pending_input()) {
        if (cmd.size())
          addLog(fmt::format("... {}\n", cmdStr));
        else
          addLog(fmt::format("<<< {}\n", cmdStr));
      } else
        addLog(fmt::format(">>> {}\n", cmdStr));

      // StdStreamRedirector redirector;
      // std::string outputStr;
      // redirector.start();
      ResourceSystem::start_cstream_capture();
      int result_ = 0;
      ZsValue ret = zs_execute_statement(cmdStr.c_str(), &result_);
      ConsoleRecord::type_e result = result_ == 0 ? ConsoleRecord::info : ConsoleRecord::error;
      ResourceSystem::dump_cstream_capture();

      PyGILState_STATE gstate = PyGILState_Ensure();
      {
        /// involes python capi calling, require GIL
        PyVar resultPyBytes = ret;
        if (resultPyBytes) {
          auto cstr = resultPyBytes.asBytes().c_str();
          ResourceSystem::push_assembled_log(cstr, result);
        } else {
          ResourceSystem::push_assembled_log("", result);
        }

        if (result_ == 5) _requestExit = true;
      }
      PyGILState_Release(gstate);
    }
  }

  void Terminal::paint() {
    ImGui::Separator();

    ///
    _filter.Draw("regex text filter", 180);
    ImGui::SameLine();
    bool copyToClipboard = ImGui::SmallButton((const char *)u8"复制");
    ImGui::SameLine();
    bool clearScreenOutput = ImGui::SmallButton((const char *)u8"清屏");
    /// @note delay this op to avoid conflicts with TextSelect
    if (clearScreenOutput) {
      ResourceSystem::clear_logs();
      addLog("Python Console\n");
    }
    ImGui::Separator();

    /// terminal content
    // Reserve enough left-over height for 1 separator + 1 input text
    const float footer_height_to_reserve
        = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, g_darker_color);

    if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove)) {
      if (ImGui::BeginPopupContextWindow()) {
        if (ImGui::Selectable("Add Message")) addLog("this is a dummy message.\n");
        ImGui::EndPopup();
      }

      // Display every line as a separate entry so we can change their color or
      // add custom widgets. If you only want raw text you can use
      // ImGui::TextUnformatted(log.begin(), log.end()); NB- if you have
      // thousands of entries this approach may be too inefficient and may
      // require user-side clipping to only process visible items. The clipper
      // will automatically measure the height of your first item and then
      // "seek" to display only items in the visible area.
      // To use the clipper we can replace your standard loop:
      //      for (int i = 0; i < Items.Size; i++)
      //   With:
      //      ImGuiListClipper clipper;
      //      clipper.Begin(Items.Size);
      //      while (clipper.Step())
      //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
      // - That your items are evenly spaced (same height)
      // - That you have cheap random access to your elements (you can access
      // them given their index,
      //   without processing all the ones before)
      // You cannot this code as-is if a filter is active because it breaks the
      // 'cheap random-access' property. We would need random-access on the
      // post-filtered list. A typical application wanting coarse clipping and
      // filtering may want to pre-compute an array of indices or offsets of
      // items that passed the filtering test, recomputing this array when user
      // changes the filter, and appending newly elements as they are inserted.
      // This is left as a task to the user until we can manage to improve this
      // example code! If your items are of variable height:
      // - Split them into same height items would be simpler and facilitate
      // random-seeking into your list.
      // - Consider using manual call to IsRectVisible() and skipping extraneous
      // decoration from your items.

      ///
      /// terminal window
      ///
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));  // Tighten spacing

      if (copyToClipboard) ImGui::LogToClipboard();

      auto wrapWidth = ImGui::GetContentRegionAvail().x;
      const auto charWidth = ImGui::CalcTextSize(" ").x;
      const u32 evaledNumChars = (u32)std::ceil(wrapWidth / charWidth);
      // ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapWidth);

      _arrangedItems.clear();
      for (auto &item : ResourceSystem::ref_logs()) {
        if (!_filter.PassFilter(item.c_str())) continue;

        // Normally you would store more information in your item than
        // just a string. (e.g. make Items[] an array of structure,
        // store color/type etc.)
        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (item.type == 0)
          color = ImVec4(0.675f, 0.84f, 0.9f, 1.0f);
        else if (item.type == 2)
          color = ImVec4(1.f, 0.49f, 0.49f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, color);

        auto itemWidth = ImGui::CalcTextSize(item.c_str()).x;
        if (itemWidth < wrapWidth) {
          _arrangedItems.emplace_back(item.data());
          ImGui::Spacing();
          ImGui::TextUnformatted(_arrangedItems.back().data(),
                                 _arrangedItems.back().data() + _arrangedItems.back().size());
        } else {
          const char *st = item.c_str();
          const char *fin = item.c_str() + item.size();
          do {
            const char *ed = st;
            u32 numSteppedChars = std::min((u32)(fin - st), evaledNumChars);
            utf8::unchecked::advance(ed, numSteppedChars);
            float curLineWidth = 0.f;
            while ((curLineWidth = ImGui::CalcTextSize(st, ed).x) >= wrapWidth)
              utf8::unchecked::advance(ed, -1);

            _arrangedItems.emplace_back(std::string_view{st, (u32)(ed - st)});
            ImGui::Spacing();
            ImGui::TextUnformatted(_arrangedItems.back().data(),
                                   _arrangedItems.back().data() + _arrangedItems.back().size());

            st = ed;
            itemWidth -= curLineWidth;
          } while (itemWidth >= wrapWidth);
          _arrangedItems.emplace_back(std::string_view{st, (u32)(fin - st)});
          ImGui::Spacing();
          ImGui::TextUnformatted(_arrangedItems.back().data(),
                                 _arrangedItems.back().data() + _arrangedItems.back().size());
        }

        ImGui::PopStyleColor();
      }
      // ImGui::PopTextWrapPos();

      if (copyToClipboard) ImGui::LogFinish();

      // Keep up at the bottom of the scroll region if we were already at the
      // bottom at the beginning of the frame. Using a scrollbar or mouse-wheel
      // will take away from the bottom edge.
      if (_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
      }
      _scrollToBottom = false;

      ImGui::PopStyleVar();

      _textSelect.update();
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();

    _reclaimFocus = false;
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
      if (ImGui::IsKeyPressed(ImGuiKey_Tab)) _reclaimFocus = true;
    }
    ImGui::Separator();

    ///
    /// command line
    ///
    ImGuiInputTextFlags inputTextFlags
        = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll
          | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
    bool pendingInput = zs_world_pending_input();
    if (pendingInput) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 205, 192));

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (_focusInput) {
      ImGui::SetKeyboardFocusHere();
      _focusInput = false;
    }

    auto &pyExec = PyExecSystem::instance();
    bool inProgress = pyExec.inProgress();
    if (inProgress) {
      ImGui::BeginDisabled(true);
      _inputBuf.clear();

      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 102, 192));
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg, g_darkest_color);
    if (ImGui::InputTextWithHint("##Input", (const char *)ICON_MD_SHORT_TEXT u8"Python脚本",
                                 &_inputBuf, inputTextFlags, &s_input_text_callback, (void *)this)
        && !inProgress) {
      {
        bool res = pyExec.assignTask([this] {
          auto len = std::strlen(_inputBuf.c_str());
          // execute command and retrieve screen output (if any)
          if (!len)
            executeCommand("");
          else
            executeCommand({_inputBuf.c_str(), len});
          _inputBuf.clear();
        });
        /// @note successfully assigned the task
        if (res) {
          _reclaimFocus = true;
        }
      }
    } else if (inProgress) {
      auto res = pyExec.tryFinish();
      if (res) {
        _reclaimFocus = true;
        if (_requestExit) PyExecSystem::request_termination();
      }
    }
    ImGui::PopStyleColor();

    if (inProgress) {
      ImGui::PopStyleColor();

      ImGui::EndDisabled();
    }

    if (pendingInput) ImGui::PopStyleColor();

    // Auto-focus on window apparition
    ImGui::SetItemDefaultFocus();
    if (_reclaimFocus) ImGui::SetKeyboardFocusHere(-1);  // Auto focus previous widget
  }

  int Terminal::inputTextCallback(ImGuiInputTextCallbackData *data) {
    switch (data->EventFlag) {
      case ImGuiInputTextFlags_CallbackCompletion:
        if (data->CursorPos == 0 || data->Buf[data->CursorPos - 1] == '\t') {
          data->InsertChars(data->CursorPos, "\t");
        } else {
          // Locate beginning of current word
          const char *word_end = data->Buf + data->CursorPos;
          const char *word_start = word_end;
          while (word_start > data->Buf) {
            const char c = word_start[-1];
            if (c == ' ' || c == '\t' || c == ',' || c == ';') break;
            word_start--;
          }

          // Build a list of candidates
          std::vector<std::string_view> candidates;
          size_t len = (word_end - word_start);
          if (len)
            for (int i = 0; i != _commands.size(); i++) {
              if (std::string_view(_commands[i].name().c_str(), len)
                  == std::string_view(word_start, len)) {
                candidates.push_back(_commands[i].name());
                // fmt::print("[{}] == [{}], thus\n",
                //            std::string_view(_commands[i].name().c_str(),
                //            len),
                ////             std::string_view(word_start, len));
              }
            }

          // No match
          if (candidates.size() == 0) {
            if (len)
              addLog(fmt::format("No match for \"{}\"!\n",
                                 std::string_view(word_start, word_end - word_start)));
          } else if (candidates.size() == 1) {
            // Single match. Delete the beginning of the word and replace
            // it entirely so we've got nice casing.
            data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
            data->InsertChars(data->CursorPos, candidates[0].data());
            data->InsertChars(data->CursorPos, " ");
          } else {
            // Multiple matches. Complete as much as we can..
            // So inputing "C"+Tab will complete to "CL" then display
            // "CLEAR" and "CLASSIFY" as matches.
            for (;;) {
              int c = toupper(candidates[0][len]);
              bool allMatch = true;
              for (int i = 1; i < candidates.size() && allMatch; i++)
                if (c != toupper(candidates[i][len])) allMatch = false;
              if (!allMatch) break;
              len++;
            }

            if (len > 0) {
              data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
              data->InsertChars(data->CursorPos, candidates[0].data(), candidates[0].data() + len);
            }

            // List matches
            addLog("Possible matches:\n");
            for (int i = 0; i != candidates.size(); i++)
              addLog(fmt::format("- {}\n", candidates[i]));
          }
        }
        break;
      case ImGuiInputTextFlags_CallbackHistory: {
        const int prevHistoryPos = _historyPos;
        if (data->EventKey == ImGuiKey_UpArrow) {
          if (_historyPos == -1)
            _historyPos = _history.size() - 1;
          else if (_historyPos > 0)
            _historyPos--;
        } else if (data->EventKey == ImGuiKey_DownArrow) {
          if (_historyPos != -1)
            if (++_historyPos >= _history.size()) _historyPos = -1;
        }

        // A better implementation would preserve the data on the current input
        // line along with cursor position.
        if (prevHistoryPos != _historyPos) {
          const std::string &historyStr = (_historyPos >= 0) ? _history.at(_historyPos) : "";
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, historyStr.data());
        }
      }
    }
    return 0;
  }

}  // namespace zs