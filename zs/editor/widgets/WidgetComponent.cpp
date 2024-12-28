#include "WidgetComponent.hpp"

#include "IconsMaterialDesign.h"
#include "IconsMaterialDesignIcons.h"
#include "IconsMaterialSymbols.h"

namespace zs {

  InternalWidget::~InternalWidget() {
    if (_ownQueue) {
      delete _msgQueue;
      _ownQueue = false;
    }
    _msgQueue = nullptr;
  }

  InternalWidget& InternalWidget::operator=(InternalWidget&& o) {
    if (_ownQueue) delete _msgQueue;
    _widget = zs::move(o._widget);
    _msgQueue = zs::exchange(o._msgQueue, nullptr);
    _ownQueue = zs::exchange(o._ownQueue, false);
    return *this;
  }

  /// AssetEntry
  const char* AssetEntry::getDisplayLabel() const {
    const char* label = (const char*)ICON_MDI_FILE;
    switch (_type) {
      case type_e::text_:
        label = (const char*)ICON_MD_SOURCE;
        break;
      case type_e::usd_:
        label = (const char*)ICON_MS_SCENE;
        break;
      default:
        break;
    }
    return label;
  }

}  // namespace zs