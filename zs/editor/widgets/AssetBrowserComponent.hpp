#pragma once

#include <deque>
#include <map>

#include "WidgetComponent.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "zensim/ZpcResource.hpp"
#include "zensim/execution/Concurrency.h"
#include "zensim/execution/ConcurrencyPrimitive.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"
#include "zensim/zpc_tpls/fmt/core.h"

namespace zs {

  struct DefaultImGuiSelection : ImGuiSelectionBasicStorage {
    inline int ApplyDeletionPreLoop(ImGuiMultiSelectIO *ms_io, int items_count);
    template <typename ITEM_TYPE> void ApplyDeletionPostLoop(ImGuiMultiSelectIO *ms_io,
                                                             std::deque<ITEM_TYPE> &items,
                                                             int item_curr_idx_to_select);
  };

  struct AssetBrowserComponent : WidgetConcept {
    using AssetEntries = std::deque<UniquePtr<AssetEntry>>;
    using AssetPtr = AssetEntry *;

    AssetBrowserComponent();
    ~AssetBrowserComponent();
    AssetBrowserComponent(AssetBrowserComponent &&o) noexcept = default;
    AssetBrowserComponent &operator=(AssetBrowserComponent &&o) noexcept = default;

    void paint() override;

    /// asset entry maintenance
    AssetPtr appendItem(const std::string &tag, AssetEntry::type_e type) {
      UniquePtr<AssetEntry> entry{new AssetEntry(tag, nextItemId(), type)};
      {
        // std::lock_guard lk{_mutex};
        auto [iter, success] = _itemMap.emplace(tag, entry.get());
        if (success) {
          _itemIdMap[entry->getId()] = entry.get();
          _items.emplace_back(zs::move(entry));
          return iter->second;
        }
      }
      return nullptr;
    }
    bool delItem(const std::string &tag) {
      if (auto mit = _itemMap.find(tag); mit != _itemMap.end()) {
        for (auto it = _items.begin(); it != _items.end(); ++it) {
          if ((*it).get() == mit->second) {
            _items.erase(it);
            _itemMap.erase(mit);
            _itemIdMap.erase(mit->second->getId());
            return true;
          }
        }
      }
      return false;
    }
    void clearItems() {
      _items.clear();
      _itemMap.clear();
      _itemIdMap.clear();
    }
    AssetPtr getItem(const std::string &tag) {
      if (auto it = _itemMap.find(tag); it != _itemMap.end()) return it->second;
      return nullptr;
    }
    AssetPtr getItem(ImGuiID id) {
      if (auto it = _itemIdMap.find(id); it != _itemIdMap.end()) return it->second;
      return nullptr;
    }

    /// gui draw helper
    void updateLayoutSizes(float avail_width);

    ImGuiID nextItemId() { return NextItemId++; }

  protected:
    // Mutex _mutex;
    AssetEntries _items;
    DefaultImGuiSelection _selection;
    std::map<std::string, AssetPtr> _itemMap;
    std::map<ImGuiID, AssetPtr> _itemIdMap;

    // ImGuiTextFilter _filter;
    // std::vector<std::string_view> _filteredItems;

    ImGuiID NextItemId = 33;      // Unique identifier when creating new items
    bool RequestDelete = false;   // Deferred deletion request
    bool RequestSort = false;     // Deferred sort request
    float ZoomWheelAccum = 0.0f;  // Mouse wheel accumulator to handle smooth wheels better

    // ui
    bool ShowTypeOverlay = true;
    bool AllowSorting = true;
    bool AllowDragUnselected = false;
    bool AllowBoxSelect = true;
    float IconSize = 128.0f;
    int IconSpacing = 16;
    int IconHitSpacing = 4;  // Increase hit-spacing if you want to make it possible to clear or
                             // box-select from gaps. Some spacing is required to able to amend with
                             // Shift+box-select. Value is small in Explorer.
    bool StretchSpacing = true;
    // ui (layout)
    ImVec2 LayoutItemSize;
    ImVec2 LayoutItemStep;  // == LayoutItemSize + LayoutItemSpacing
    float LayoutItemSpacing = 0.0f;
    float LayoutSelectableSpacing = 0.0f;
    float LayoutOuterPadding = 0.0f;
    int LayoutColumnCount = 0;
    int LayoutLineCount = 0;
  };

  // Find which item should be Focused after deletion.
  // Call _before_ item submission. Retunr an index in the before-deletion item list, your item
  // loop should call SetKeyboardFocusHere() on it. The subsequent ApplyDeletionPostLoop() code
  // will use it to apply Selection.
  // - We cannot provide this logic in core Dear ImGui because we don't have access to selection
  // data.
  // - We don't actually manipulate the ImVector<> here, only in ApplyDeletionPostLoop(), but
  // using similar API for consistency and flexibility.
  // - Important: Deletion only works if the underlying ImGuiID for your items are stable: aka not
  // depend on their index, but on e.g. item id/ptr.
  // FIXME-MULTISELECT: Doesn't take account of the possibility focus target will be moved during
  // deletion. Need refocus or scroll offset.
  int DefaultImGuiSelection::ApplyDeletionPreLoop(ImGuiMultiSelectIO *ms_io, int items_count) {
    if (Size == 0) return -1;

    // If focused item is not selected...
    const int focused_idx = (int)ms_io->NavIdItem;  // Index of currently focused item
    if (ms_io->NavIdSelected == false)              // This is merely a shortcut, ==
                                        // Contains(adapter->IndexToStorage(items, focused_idx))
    {
      ms_io->RangeSrcReset
          = true;  // Request to recover RangeSrc from NavId next frame. Would be ok to reset even
                   // when NavIdSelected==true, but it would take an extra frame to recover
                   // RangeSrc when deleting a selected item.
      return focused_idx;  // Request to focus same item after deletion.
    }

    // If focused item is selected: land on first unselected item after focused item.
    for (int idx = focused_idx + 1; idx < items_count; idx++)
      if (!Contains(GetStorageIdFromIndex(idx))) return idx;

    // If focused item is selected: otherwise return last unselected item before focused item.
    for (int idx = std::min(focused_idx, items_count) - 1; idx >= 0; idx--)
      if (!Contains(GetStorageIdFromIndex(idx))) return idx;

    return -1;
  }

  void close_usd_asset(std::string_view label);
  void close_script_asset(const std::string &label);

  // Rewrite item list (delete items) + update selection.
  // - Call after EndMultiSelect()
  // - We cannot provide this logic in core Dear ImGui because we don't have access to your items,
  // nor to selection data.
  template <typename ITEM_TYPE>
  void DefaultImGuiSelection::ApplyDeletionPostLoop(ImGuiMultiSelectIO *ms_io,
                                                    std::deque<ITEM_TYPE> &items,
                                                    int item_curr_idx_to_select) {
    // Rewrite item list (delete items) + convert old selection index (before deletion) to new
    // selection index (after selection). If NavId was not part of selection, we will stay on same
    // item.
    std::deque<ITEM_TYPE> new_items;
    // new_items.reserve(items.size() - Size);
    int item_next_idx_to_select = -1;
    for (int idx = 0; idx < items.size(); idx++) {
      auto &item = items[idx];
      if (!Contains(GetStorageIdFromIndex(idx)))
        new_items.push_back(zs::move(items[idx]));
      else {
        switch (item->getType()) {
          case AssetEntry::type_e::usd_:
            close_usd_asset(item->getLabel());
            break;
          case AssetEntry::type_e::text_:
            close_script_asset(std::string(item->getLabel()));
          default:
            throw std::runtime_error("unknown type of the closed assets.");
        }
      }
      if (item_curr_idx_to_select == idx) item_next_idx_to_select = new_items.size() - 1;
    }
    zs_swap(items, new_items);

    // Update selection
    Clear();
    if (item_next_idx_to_select != -1 && ms_io->NavIdSelected)
      SetItemSelected(GetStorageIdFromIndex(item_next_idx_to_select), true);
  }

}  // namespace zs