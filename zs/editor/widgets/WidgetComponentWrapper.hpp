#pragma once
#include "WidgetComponent.hpp"

namespace zs {

  template <typename Widget> struct WidgetComponentWrapper : WidgetComponentConcept {
    void paint() override { _embeddedWidget->paint(); }

    std::string _registryLabel;  // in ResourceSystem
    Shared<WidgetComponentConcept> _embeddedWidget;
  };

}  // namespace zs