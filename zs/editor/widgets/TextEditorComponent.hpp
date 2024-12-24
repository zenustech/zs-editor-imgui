#pragma once

#include "WidgetComponent.hpp"
#include "imgui.h"
#include "zensim/ZpcResource.hpp"
#include "zensim/execution/Concurrency.h"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct TextEditor : WidgetComponentConcept {
    TextEditor();
    ~TextEditor();

    void paint() override;

  protected:
    std::string _buffer;

    TabEntries<std::string> _tabs;
  };

}  // namespace zs