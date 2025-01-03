#include "WidgetEvent.hpp"

#include "imgui_internal.h"

namespace zs {

  bool KeyEvent::isModKey() const noexcept { return ImGui::IsLRModKey(_key); }

}  // namespace zs