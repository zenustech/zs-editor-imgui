#pragma once
#include "imgui.h"

namespace zs {

  enum ItemLabelFlag {
    Left = 1u << 0u,
    Right = 1u << 1u,
    Default = Left,
  };

  inline ImVec4 g_darkest_color(0.f, 0.f, 0.f, 1.f);
  inline ImVec4 g_almost_dark_color(0.02f, 0.02f, 0.02f, 1.f);
  inline ImVec4 g_darker_color(0.04f, 0.04f, 0.04f, 1.f);
  inline ImVec4 g_dark_color(0.1f, 0.1f, 0.1f, 1.f);
  inline ImVec4 g_dim_color(0.2f, 0.2f, 0.2f, 1.f);

  inline ImVec4 g_green_color(0.f, 0.9f, 0.f, 0.8f);
  inline ImVec4 g_light_green_color(0.5f, 0.9f, 0.5f, 0.8f);
  inline ImVec4 g_dark_green_color(0.0234f, 0.25f, 0.168f, 1.f);

  inline ImVec4 g_light_color(0.9f, 0.9f, 0.9f, 1.f);

}  // namespace zs