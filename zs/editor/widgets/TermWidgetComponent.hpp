#pragma once
#include <deque>

#include "world/system/ResourceSystem.hpp"
#include "imgui.h"
#include "utilities/textselect.hpp"
#include "zensim/ZpcResource.hpp"
#include "zensim/execution/Concurrency.h"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"
#include "zensim/zpc_tpls/fmt/format.h"

namespace zs {

  struct Command {
    Command(std::string_view name, std::string_view descr = "") : _name{name}, _descr{descr} {}

    const std::string &name() const noexcept { return _name; }

    std::string _name;
    std::string _descr;
  };

  struct TerminalBackendConcept {
    virtual ~TerminalBackendConcept() = default;
  };

  struct Terminal {
    std::string _inputBuf;
    bool _requestExit;
    bool _reclaimFocus, _focusInput;

    /// @note the actual content to be displayed could be accessed through ResourceSystem
    std::vector<std::string_view> _arrangedItems;

    std::vector<Command> _commands;

    std::deque<std::string> _history;
    u32 _historyLimit;
    int _historyPos;

    ImGuiTextFilter _filter;
    TextSelect _textSelect;

    bool _scrollToBottom;

    UniquePtr<TerminalBackendConcept> _backend;

    Terminal();
    ~Terminal();

    void addLog(std::string_view msg, ConsoleRecord::type_e type = ConsoleRecord::unknown) {
      ResourceSystem::push_log(msg, type);
    }

    void paint();
    void executeCommand(std::string_view cmd);

  protected:
    static int s_input_text_callback(ImGuiInputTextCallbackData *data) {
      return static_cast<Terminal *>(data->UserData)->inputTextCallback(data);
    }
    int inputTextCallback(ImGuiInputTextCallbackData *data);
  };

  struct TerminalWidgetComponent : WidgetComponentConcept {
    TerminalWidgetComponent() = default;
    TerminalWidgetComponent(std::string_view label, Shared<Terminal> &term)
        : _label{label}, _term{term} {}
    ~TerminalWidgetComponent() = default;
    TerminalWidgetComponent(TerminalWidgetComponent &&) = default;
    TerminalWidgetComponent &operator=(TerminalWidgetComponent &&) = default;

    void paint() override {
      auto term = _term.lock();
      term->paint();
    }

    std::string _label;
    Weak<Terminal> _term;
  };

}  // namespace zs