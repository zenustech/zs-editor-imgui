#include "WidgetComponent.hpp"

#include "IconsMaterialDesign.h"
#include "IconsMaterialDesignIcons.h"
#include "IconsMaterialSymbols.h"

namespace zs {

  GuiEventHub::~GuiEventHub() {
    if (_ownQueue) {
      delete _msgQueue;
      _ownQueue = false;
    }
    _msgQueue = nullptr;
  }

  GuiEventHub& GuiEventHub::operator=(GuiEventHub&& o) {
    if (_ownQueue) delete _msgQueue;
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