#include "AssetBrowserComponent.hpp"

#include "editor/widgets/WidgetDrawUtilities.hpp"
//

// #include "IconsFontAwesome6.h"
#include "world/core/Utils.hpp"
#include "imgui.h"
#include "imgui_stdlib.h"
#include "world/system/ResourceSystem.hpp"

namespace zs {

  AssetBrowserComponent::AssetBrowserComponent() {
    /// usd assets
    ResourceSystem::onUsdFilesOpened().connect([this](const std::vector<std::string>& fileLabels) {
      for (auto& label : fileLabels) {
        // fmt::print("\tadded usd file with label: {}!\n", file);
        // _useSceneNames
        this->appendItem(label, AssetEntry::type_e::usd_);
      }
    });
    this->appendItem(g_defaultUsdLabel, AssetEntry::type_e::usd_);

    /// script assets
    ResourceSystem::onScriptFilesOpened().connect([this](const std::vector<std::string>& labels) {
      for (auto& label : labels) {
        this->appendItem(label, AssetEntry::type_e::text_);
      }
    });
    this->appendItem(g_textEditorLabel, AssetEntry::type_e::text_);

    /// texture assets
    ResourceSystem::onTexturesAdded().connect([this](const std::vector<std::string>& labels) {
      for (auto& label : labels) {
        this->appendItem(label, AssetEntry::type_e::texture_);
      }
    });
  }
  AssetBrowserComponent::~AssetBrowserComponent() {}

  void AssetBrowserComponent::paint() {
    // std::lock_guard lk{_mutex};
    ImGui::SetNextWindowSize(ImVec2(IconSize * 25, IconSize * 15), ImGuiCond_FirstUseEver);

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowContentSize(ImVec2(
        0.0f, LayoutOuterPadding + LayoutLineCount * (LayoutItemSize.x + LayoutItemSpacing)));
    if (ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()),
                          ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove)) {
      ImDrawList* draw_list = ImGui::GetWindowDrawList();

      const float avail_width = ImGui::GetContentRegionAvail().x;
      updateLayoutSizes(avail_width);

      // Calculate and store start position.
      ImVec2 start_pos = ImGui::GetCursorScreenPos();
      start_pos = ImVec2(start_pos.x + LayoutOuterPadding, start_pos.y + LayoutOuterPadding);
      ImGui::SetCursorScreenPos(start_pos);

      ///
      /// Multi-select
      ///
      ImGuiMultiSelectFlags ms_flags
          = ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_ClearOnClickVoid;

      // - Enable box-select (in 2D mode, so that changing box-select rectangle X1/X2 boundaries
      // will affect clipped items)
      if (AllowBoxSelect) ms_flags |= ImGuiMultiSelectFlags_BoxSelect2d;

      // - This feature allows dragging an unselected item without selecting it (rarely used)
      if (AllowDragUnselected) ms_flags |= ImGuiMultiSelectFlags_SelectOnClickRelease;

      // - Enable keyboard wrapping on X axis
      // (FIXME-MULTISELECT: We haven't designed/exposed a general nav wrapping api yet, so this
      // flag is provided as a courtesy to avoid doing:
      //    ImGui::NavMoveRequestTryWrapping(ImGui::GetCurrentWindow(), ImGuiNavMoveFlags_WrapX);
      // When we finish implementing a more general API for this, we will obsolete this flag in
      // favor of the new system)
      ms_flags |= ImGuiMultiSelectFlags_NavWrapX;

      ImGuiMultiSelectIO* ms_io = ImGui::BeginMultiSelect(ms_flags, _selection.Size, _items.size());

      // Use custom selection adapter: store ID in selection (recommended)
      _selection.UserData = this;
      _selection.AdapterIndexToStorageId
          = [](ImGuiSelectionBasicStorage* self_, int idx) -> ImGuiID {
        AssetBrowserComponent* self = (AssetBrowserComponent*)self_->UserData;
        return self->_items[idx]->getId();
      };
      _selection.ApplyRequests(ms_io);

      const bool want_delete
          = (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_Repeat) && (_selection.Size > 0))
            || RequestDelete;
      const int item_curr_idx_to_focus
          = want_delete ? _selection.ApplyDeletionPreLoop(ms_io, _items.size()) : -1;
      RequestDelete = false;

      // Push LayoutSelectableSpacing (which is LayoutItemSpacing minus hit-spacing, if we decide to
      // have hit gaps between items) Altering style ItemSpacing may seem unnecessary as we position
      // every items using SetCursorScreenPos()... But it is necessary for two reasons:
      // - Selectables uses it by default to visually fill the space between two items.
      // - The vertical spacing would be measured by Clipper to calculate line height if we didn't
      // provide it explicitly (here we do).
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                          ImVec2(LayoutSelectableSpacing, LayoutSelectableSpacing));
      ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_NavHighlight, g_green_color);
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, g_dark_green_color);  // g_darkest_color
      ImGui::PushStyleColor(ImGuiCol_Header, g_dark_green_color);
      ImGui::SetWindowFontScale(LayoutItemSize.y / ImGui::GetFontSize());

      // Rendering parameters
      const ImU32 icon_type_overlay_colors[3]
          = {0, IM_COL32(200, 70, 70, 255), IM_COL32(70, 170, 70, 255)};
      const ImU32 icon_bg_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
      const ImVec2 icon_type_overlay_size = ImVec2(4.0f, 4.0f);
      // const bool display_label = (LayoutItemSize.x >= ImGui::CalcTextSize("999").x);

      const int column_count = LayoutColumnCount;
      ImGuiListClipper clipper;
      clipper.Begin(LayoutLineCount, LayoutItemStep.y);
      if (item_curr_idx_to_focus != -1)
        clipper.IncludeItemByIndex(item_curr_idx_to_focus
                                   / column_count);  // Ensure focused item line is not clipped.
      if (ms_io->RangeSrcItem != -1)
        clipper.IncludeItemByIndex((int)ms_io->RangeSrcItem
                                   / column_count);  // Ensure RangeSrc item line is not clipped.
      while (clipper.Step()) {
        for (int line_idx = clipper.DisplayStart; line_idx < clipper.DisplayEnd; line_idx++) {
          const int item_min_idx_for_current_line = line_idx * column_count;
          const int item_max_idx_for_current_line
              = std::min((line_idx + 1) * column_count, (int)_items.size());
          for (int item_idx = item_min_idx_for_current_line;
               item_idx < item_max_idx_for_current_line; ++item_idx) {
            AssetEntry* item_data = _items[item_idx].get();
            ImGui::PushID((int)item_data->getId());

            // Position item
            ImVec2 pos = ImVec2(start_pos.x + (item_idx % column_count) * LayoutItemStep.x,
                                start_pos.y + line_idx * LayoutItemStep.y);
            ImGui::SetCursorScreenPos(pos);

            ImGui::SetNextItemSelectionUserData(item_idx);
            bool item_is_selected = _selection.Contains((ImGuiID)item_data->getId());
            bool item_is_visible = ImGui::IsRectVisible(LayoutItemSize);
            ImGui::Selectable("", item_is_selected, ImGuiSelectableFlags_None, LayoutItemSize);

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone)) {
              std::string_view tip = "";
              switch (item_data->_type) {
                case AssetEntry::text_:
                  tip = ResourceSystem::get_script_filename(std::string(item_data->getLabel()));
                  break;
                case AssetEntry::usd_:
                  tip = item_data->getLabel();
                  break;
                case AssetEntry::texture_:
                  tip = item_data->getLabel();
                  break;
              }
              if (!tip.empty()) ImGui::SetItemTooltip(tip.data());
            }

            // Update our selection state immediately (without waiting for EndMultiSelect()
            // requests) because we use this to alter the color of our text/icon.
            if (ImGui::IsItemToggledSelection()) item_is_selected = !item_is_selected;

            // Focus (for after deletion)
            if (item_curr_idx_to_focus == item_idx) ImGui::SetKeyboardFocusHere(-1);

            // Drag and drop
            if (ImGui::BeginDragDropSource()) {
              // Create payload with full selection OR single unselected item.
              // (the later is only possible when using ImGuiMultiSelectFlags_SelectOnClickRelease)
              if (ImGui::GetDragDropPayload() == NULL) {
                ImVector<AssetEntry> payloadItems;
                void* it = NULL;
                ImGuiID id = 0;
                if (!item_is_selected)
                  payloadItems.push_back(*item_data);
                else {
                  while (_selection.GetNextSelectedItem(&it, &id))
                    payloadItems.push_back(*getItem(id));
                }
                ImGui::SetDragDropPayload("ASSETS_BROWSER_ITEMS", payloadItems.Data,
                                          (size_t)payloadItems.size_in_bytes());
              }

              // Display payload content in tooltip, by extracting it from the payload data
              // (we could read from selection, but it is more correct and reusable to read from
              // payload)
              const ImGuiPayload* payload = ImGui::GetDragDropPayload();
              const int payloadCount = (int)payload->DataSize / (int)sizeof(AssetEntry);
              ImGui::Text((const char*)u8"%d 资产", payloadCount);

              ImGui::EndDragDropSource();
            }

            // Render icon (a real app would likely display an image/thumbnail here)
            // Because we use ImGuiMultiSelectFlags_BoxSelect2d, clipping vertical may occasionally
            // be larger, so we coarse-clip our rendering as well.
            if (item_is_visible) {
              ImVec2 box_min(pos.x - 1, pos.y - 1);
              ImVec2 box_max(box_min.x + LayoutItemSize.x + 2,
                             box_min.y + LayoutItemSize.y + 2);  // Dubious
              draw_list->AddRectFilled(box_min, box_max, icon_bg_color,
                                       ImGui::GetStyle().FrameRounding);  // Background color
              if (ShowTypeOverlay && item_data->_type != 0) {
                ImU32 type_col = icon_type_overlay_colors[item_data->_type
                                                          % IM_ARRAYSIZE(icon_type_overlay_colors)];
                draw_list->AddRectFilled(
                    ImVec2(box_max.x - 2 - icon_type_overlay_size.x, box_min.y + 2),
                    ImVec2(box_max.x - 2, box_min.y + 2 + icon_type_overlay_size.y), type_col);
              }
              const char* label = item_data->getDisplayLabel();
              auto sz = ImGui::CalcTextSize(label);
              auto box_center = (box_min + box_max) / 2;
              // if (display_label)
              ImU32 label_col
                  = ImGui::GetColorU32(item_is_selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);
              draw_list->AddText(ImVec2(box_center.x - sz.x / 2, box_center.y - sz.y / 2),
                                 label_col, label);
            }

            ImGui::PopID();
          }
        }
      }
      clipper.End();
      ImGui::SetWindowFontScale(1.f);
      ImGui::PopStyleColor(3);
      ImGui::PopStyleVar(2);  // ImGuiStyleVar_ItemSpacing

      // Context menu
      if (ImGui::BeginPopupContextWindow()) {
        ImGui::Text("Selection: %d items", _selection.Size);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, _selection.Size > 0)) RequestDelete = true;
        ImGui::EndPopup();
      }

      ms_io = ImGui::EndMultiSelect();
      _selection.ApplyRequests(ms_io);
      if (want_delete) _selection.ApplyDeletionPostLoop(ms_io, _items, item_curr_idx_to_focus);

      // Zooming with CTRL+Wheel
      if (ImGui::IsWindowAppearing()) ZoomWheelAccum = 0.0f;
      if (ImGui::IsWindowHovered() && io.MouseWheel != 0.0f && ImGui::IsKeyDown(ImGuiMod_Ctrl)
          && ImGui::IsAnyItemActive() == false) {
        ZoomWheelAccum += io.MouseWheel;
        if (fabsf(ZoomWheelAccum) >= 1.0f) {
          // Calculate hovered item index from mouse location
          // FIXME: Locking aiming on 'hovered_item_idx' (with a cool-down timer) would ensure zoom
          // keeps on it.
          const float hovered_item_nx
              = (io.MousePos.x - start_pos.x + LayoutItemSpacing * 0.5f) / LayoutItemStep.x;
          const float hovered_item_ny
              = (io.MousePos.y - start_pos.y + LayoutItemSpacing * 0.5f) / LayoutItemStep.y;
          const int hovered_item_idx
              = ((int)hovered_item_ny * LayoutColumnCount) + (int)hovered_item_nx;
          // ImGui::SetTooltip("%f,%f -> item %d", hovered_item_nx, hovered_item_ny,
          // hovered_item_idx); // Move those 4 lines in block above for easy debugging

          // Zoom
          IconSize *= powf(1.1f, (float)(int)ZoomWheelAccum);
          IconSize = std::clamp(IconSize, 16.0f, 128.0f);
          ZoomWheelAccum -= (int)ZoomWheelAccum;
          updateLayoutSizes(avail_width);

          // Manipulate scroll to that we will land at the same Y location of currently hovered
          // item.
          // - Calculate next frame position of item under mouse
          // - Set new scroll position to be used in next ImGui::BeginChild() call.
          float hovered_item_rel_pos_y
              = ((float)(hovered_item_idx / LayoutColumnCount) + fmodf(hovered_item_ny, 1.0f))
                * LayoutItemStep.y;
          hovered_item_rel_pos_y += ImGui::GetStyle().WindowPadding.y;
          float mouse_local_y = io.MousePos.y - ImGui::GetWindowPos().y;
          ImGui::SetScrollY(hovered_item_rel_pos_y - mouse_local_y);
        }
      }
    }
    ImGui::EndChild();
    ImGui::Text("%d items in total, selected %d", (int)_items.size(), (int)_selection.Size);
  }

  void AssetBrowserComponent::updateLayoutSizes(float avail_width) {
    // Layout: when not stretching: allow extending into right-most spacing.
    LayoutItemSpacing = (float)IconSpacing;
    if (StretchSpacing == false) avail_width += floorf(LayoutItemSpacing * 0.5f);

    // Layout: calculate number of icon per line and number of lines
    LayoutItemSize = ImVec2(floorf(IconSize), floorf(IconSize));
    LayoutColumnCount = std::max((int)(avail_width / (LayoutItemSize.x + LayoutItemSpacing)), 1);
    LayoutLineCount = (_items.size() + LayoutColumnCount - 1) / LayoutColumnCount;

    // Layout: when stretching: allocate remaining space to more spacing. Round before division,
    // so item_spacing may be non-integer.
    if (StretchSpacing && LayoutColumnCount > 1)
      LayoutItemSpacing
          = floorf(avail_width - LayoutItemSize.x * LayoutColumnCount) / LayoutColumnCount;

    LayoutItemStep
        = ImVec2(LayoutItemSize.x + LayoutItemSpacing, LayoutItemSize.y + LayoutItemSpacing);
    LayoutSelectableSpacing = std::max(std::floor(LayoutItemSpacing) - IconHitSpacing, 0.0f);
    LayoutOuterPadding = floorf(LayoutItemSpacing * 0.5f);
  }

  /// utils
  void close_usd_asset(std::string_view label) {
#if ZPC_USD_ENABLED
    ResourceSystem::close_usd(std::string{label});
#endif
  }
  void close_script_asset(const std::string& label) { ResourceSystem::close_script(label); }

}  // namespace zs