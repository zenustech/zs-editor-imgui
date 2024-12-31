#pragma once
#include "WidgetComponent.hpp"

namespace zs {

  template <typename Widget> struct WidgetComponentWrapper : WidgetConcept {
    void paint() override { _embeddedWidget->paint(); }

    std::string _registryLabel;  // in ResourceSystem
    Shared<WidgetConcept> _embeddedWidget;
  };

}  // namespace zs