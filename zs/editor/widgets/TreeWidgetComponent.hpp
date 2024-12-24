#pragma once
#include <list>
#include <map>
#include <set>

#include "IconsMaterialDesign.h"
// #include "IconsMaterialDesignIcons.h"
#include "IconsMaterialSymbols.h"
#include "editor/ImguiSystem.hpp"
#include "WidgetBase.hpp"
#include "imgui.h"
#include "editor/widgets/WidgetComponent.hpp"
#include "zensim/ZpcFunction.hpp"
#include "zensim/ZpcImplPattern.hpp"
#include "zensim/types/ImplPattern.hpp"

//
#include "world/usd/USDScenePrim.hpp"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct ZsPrimitive;
  namespace ui {

    // key : u32 index, string, pointer
    struct TreeNodeConcept : WidgetComponentConcept {
      ~TreeNodeConcept() = default;
      virtual u32 numChildren() const = 0;
      virtual const char *getLabel() const = 0;
      virtual u32 numViewedChildren() const = 0;
      /// @note return nullptr upon failure
      virtual TreeNodeConcept *getChildByIndex(u32) = 0;
      /// @note return nullptr if it is root
      virtual TreeNodeConcept *getParent() = 0;
      virtual void setParent(TreeNodeConcept *par) = 0;

      virtual bool appendChild(TreeNodeConcept *) { return false; }
      virtual TreeNodeConcept *clone() { return nullptr; }

      virtual bool removeChildByIndex(u32) { return false; }
      virtual bool removeChildrenIf(zs::function_ref<bool(TreeNodeConcept *)> pred) {
        return false;
      }

      virtual void setWidgetId(ImGuiID id) = 0;
      virtual bool selectable() { return true; }
      virtual bool togglable() { return true; }
      virtual void onDelete(std::string_view path) {}

      // virtual void sortChildren(zs::function_ref<void()>) = 0;
      // virtual void filterChildren(zs::function<bool(TreeNodeConcept *)>) = 0;

      bool isLeaf() const { return numChildren() == 0; }
      bool isRoot() const { return const_cast<TreeNodeConcept *>(this)->getParent() == nullptr; }

      virtual i32 getViewIndexInParent() {
        auto par = getParent();
        if (par != nullptr)
          for (i32 i = 0; i < par->numViewedChildren(); ++i) {
            if (par->getChildByIndex(i) == this) return i;
          }
        return -1;
      }
    };

    struct TreeNodeTrailingOptions {
      /// @note in future, there should be other type of option specifier widgets (i.e.combos)
      enum type_e { _visible = 0, _editable, _code, _grid, _hdr, _num_types };
      static constexpr const char *icons[_num_types][2] = {
          {ICON_MD_VISIBILITY, ICON_MD_VISIBILITY_OFF},
          {ICON_MD_EDIT, ICON_MD_EDIT_OFF},
          {ICON_MD_CODE, ICON_MD_CODE_OFF},
          {ICON_MD_GRID_ON, ICON_MD_GRID_OFF},
          {ICON_MD_HDR_ON, ICON_MD_HDR_OFF},
      };

      TreeNodeTrailingOptions(type_e type = _visible, bool state = true) noexcept
          : _type{type}, _state{state} {}

      void paint() {
        auto label = icons[_type][(int)_state];
        // ImGui::SetNextItemAllowOverlap();
        if (ImGui::Button(label)) {
          _state ^= 1;
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }
      float evalWidth() const { return ImGui::CalcTextSize(icons[_type][(int)_state]).x; }

      type_e _type;
      bool _state;
    };

    /// @note key's operator< must be defined
    struct TreeNodeBase : TreeNodeConcept {
      u32 numChildren() const override { return _treeNodes.size(); }
      const char *getLabel() const override { return _label.c_str(); }
      u32 numViewedChildren() const override { return _viewedTreeNodes.size(); }

      TreeNodeConcept *getChildByIndex(u32 id) override {
        if (id < _viewedTreeNodes.size()) return _viewedTreeNodes[id];
        return nullptr;
      }
      TreeNodeConcept *getParent() override { return _parent; }
      void setParent(TreeNodeConcept *par) override { _parent = par; }

      bool appendChild(TreeNodeConcept *node) override {
        auto &ref = _treeNodes.emplace_back(node);
        _viewedTreeNodes.emplace_back(ref.get());
        // ref->setParent(_parent);
        ref->setParent(this);  // fix
        return true;
      }

      bool removeChildByIndex(u32 id) override {
        auto target = getChildByIndex(id);
        if (target) {
          _viewedTreeNodes.erase(_viewedTreeNodes.begin() + id);
          for (auto it = _treeNodes.begin(); it != _treeNodes.end();) {
            if ((*it).get() == target) {
              it = _treeNodes.erase(it);
              return true;
            } else
              ++it;
          }
        }
        return false;
      }
      bool removeChildrenIf(zs::function_ref<bool(TreeNodeConcept *)> pred) override {
        bool deleted = false;
        for (auto it = _viewedTreeNodes.begin(); it != _viewedTreeNodes.end();) {
          if (pred(*it)) {
            it = _viewedTreeNodes.erase(it);
            deleted = true;
            for (auto nodeit = _treeNodes.begin(); nodeit != _treeNodes.end();) {
              if ((*nodeit).get() == *it) {
                nodeit = _treeNodes.erase(nodeit);
                break;
              } else
                ++it;
            }
          } else
            ++it;
        }
        return deleted;
      }

      void setWidgetId(ImGuiID id) override { _id = id; }
      // bool selectable() override { return static_cast<bool>(_selection); }

      void setLabel(std::string_view label) { _label = label; }
      void setTrailingOptions(TreeNodeTrailingOptions::type_e type, bool defaultState) {
        _trailingOptions[type] = TreeNodeTrailingOptions{type, defaultState};
      }
      // void setSelectionSet(std::shared_ptr<std::set<TreeNodeConcept *>> set) { _selection = set;
      // } bool isSelected() { return selectable() && (*_selection).find(this) !=
      // (*_selection).end(); }

      void paint() override;

      void paintNode(ImGuiSelectionBasicStorage *selection, const std::string &path = "/");

      TreeNodeConcept *_parent{nullptr};
      ImGuiID _id;
      std::string _label;
      std::list<Unique<TreeNodeConcept>> _treeNodes;
      std::vector<TreeNodeConcept *> _viewedTreeNodes;
      std::map<TreeNodeTrailingOptions::type_e, TreeNodeTrailingOptions> _trailingOptions;
      ImGuiTreeNodeFlags _flags{
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
          | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap};
      // std::shared_ptr<std::set<TreeNodeConcept *>> _selection;
      float _width;

      ImGuiSelectionBasicStorage _imguiSelection;
    };

    template <typename TreeNode> struct TreeBuilder {
      static_assert(is_base_of_v<TreeNodeConcept, TreeNode>,
                    "TreeNode should be inherited from TreeNodeConcept");

      TreeBuilder(Shared<IDGenerator> id, TreeNode &node)
          : _node{node},
            _parentBuilder{nullptr},
            _curId{id} /*,_selection{std::make_shared<std::set<TreeNodeConcept *>>()}*/
      {
        node.setWidgetId(id->nextId());
      }
      TreeBuilder(Shared<IDGenerator> id, TreeNode &node, TreeBuilder *par)
          : _node{node}, _parentBuilder{par}, _curId{id} /*, _selection{par->_selection}*/ {
        node.setWidgetId(id->nextId());
      }

      template <typename NodeType = TreeNode, typename... Args,
                enable_if_t<is_base_of_v<TreeNodeConcept, NodeType>> = 0>
      auto beginChild(Args &&...args) {
        auto n = _node.numChildren();
        NodeType *child = new NodeType(FWD(args)...);
        _node.appendChild(child);  // setParent happens within
        // fmt::print("\n\ntree child [{}] add parent [{}]\n", (void *)child, (void *)&_node);
        return TreeBuilder<NodeType>{_curId, *child, this};
      }

      /// @note custom setup
      template <typename Node = TreeNode>
      auto setLabel(std::string_view label) -> decltype(declval<Node &>().setLabel(label), *this) {
        _node.setLabel(label);
        return *this;
      }
      template <typename Node = TreeNode>
      auto setOption(TreeNodeTrailingOptions::type_e type,
                     bool state) -> decltype(declval<Node &>().setTrailingOptions(type, state),
                                             *this) {
        _node.setTrailingOptions(type, state);
        return *this;
      }
#if 0
      template <typename Node = TreeNode> auto enableSelection()
          -> decltype(declval<Node &>().setSelectionSet(
                          declval<std::shared_ptr<std::set<TreeNodeConcept *>>>()),
                      *this) {
        // _node.setSelectionSet(_selection);
        return *this;
      }
#endif
      TreeBuilder &setupObject(function_ref<void(TreeNode &)> op) {
        op(_node);
        return *this;
      }

      TreeBuilder &endChild() {
        if (_parentBuilder)
          return *_parentBuilder;
        else
          return *this;
      }
      /// @note helper func that combines endChild + beginChild
      template <typename NodeType = TreeNode, typename... Args, typename Self = TreeBuilder,
                enable_if_t<is_base_of_v<TreeNodeConcept, NodeType>> = 0>
      auto nextChild(Args &&...args)
          -> decltype(declval<Self &>().endChild().beginChild(FWD(args)...)) {
        return endChild().beginChild(FWD(args)...);
      }

      /// @note might be a raw pointer
      TreeNode *get() { return &_node; }
      operator TreeNode *() && { return &_node; }

      TreeNode &_node;
      TreeBuilder *_parentBuilder{nullptr};
      Shared<IDGenerator> _curId;
      // std::shared_ptr<std::set<TreeNodeConcept *>> _selection;
    };

    template <typename TreeNode, typename... Args,
              enable_if_t<is_base_of_v<TreeNodeConcept, TreeNode>> = 0>
    TreeBuilder<TreeNode> build_tree_node(Args &&...args) {
      auto nodePtr = new TreeNode(FWD(args)...);
      return TreeBuilder<TreeNode>{std::make_shared<IDGenerator>(), *nodePtr};
    }

    struct FileTreeNode : TreeNodeBase {
      enum type_e { _unknown = 0, _dir, _text, _img, _video, _exe, _module, _num_types };
      static constexpr const char *icons[_num_types]
          = {ICON_MS_UNKNOWN_DOCUMENT, ICON_MD_FOLDER,   ICON_MD_SOURCE,       ICON_MD_IMAGE,
             ICON_MD_VIDEO_FILE,       ICON_MD_TERMINAL, ICON_MS_DEPLOYED_CODE};
      // ICON_MD_TEXT_SNIPPET

      FileTreeNode &setType(type_e t = type_e::_unknown) {
        _type = t;
        return *this;
      }
      FileTreeNode &setLabel(std::string_view msg) {
        TreeNodeBase::setLabel(std::string(icons[_type]) + std::string(msg));
        return *this;
      }

      type_e _type{_unknown};
    };

    struct UsdTreeNode : TreeNodeBase {
      enum type_e { _unknown = 0, _cam, _light, _mesh, _sdf, _curve, _transform, _num_types };
      static constexpr const char *icons[_num_types]
          = {ICON_MD_ACCOUNT_TREE, ICON_MD_CAMERA,
             ICON_MD_LIGHT,        /*ICON_MDI_CUBE*/ ICON_MD_WIDGETS,
             ICON_MD_GRAIN,        ICON_MS_LINE_CURVE,
             ICON_MD_TRANSFORM};

      UsdTreeNode &setType(type_e t = type_e::_unknown) {
        _type = t;
        return *this;
      }
      UsdTreeNode &setLabel(std::string_view msg) {
        // TreeNodeBase::setLabel(std::string(icons[_type]) + std::string(msg));
        TreeNodeBase::setLabel(std::string(msg));
        return *this;
      }
      void onDelete(std::string_view path) override;

      SceneDescConcept *_scene{nullptr};  // plugin->getScene(zs_cstr(_name.data()))
      std::string _path{""}, _sceneName{""};
      type_e _type{_unknown};
    };

    /// @note new reference
    UsdTreeNode *build_usd_tree_node(ScenePrimConcept *pr);

    struct SceneFileEditor : WidgetComponentConcept {
      SceneFileEditor();
      ~SceneFileEditor();

      struct Meta {
        SceneDescConcept *_scene;
      };

      void paint() override;

    protected:
      TabEntries<Meta> _tabs;
    };

    struct PrimitiveTreeNode : TreeNodeBase {
      enum type_e { _general = 0, _cam, _light, _mesh, _sdf, _analytic, _transform, _num_types };
      static constexpr const char *icons[_num_types]
          = {ICON_MD_ACCOUNT_TREE, ICON_MD_CAMERA,
             ICON_MD_LIGHT,        /*ICON_MDI_CUBE*/ ICON_MD_WIDGETS,
             ICON_MD_GRAIN,        ICON_MS_LINE_CURVE,
             ICON_MD_TRANSFORM};

      PrimitiveTreeNode &setType(type_e t = type_e::_general) {
        _type = t;
        return *this;
      }
      PrimitiveTreeNode &setLabel(std::string_view msg) {
        // TreeNodeBase::setLabel(std::string(icons[_type]) + std::string(msg));
        TreeNodeBase::setLabel(std::string(msg));
        return *this;
      }
      void onDelete(std::string_view path) override;

      Weak<ZsPrimitive> _prim{};
      std::string _path{""};
      type_e _type{_general};
    };

    /// @note new reference
    PrimitiveTreeNode *build_primitive_tree_node(Weak<ZsPrimitive> pr);

    struct PrimitiveEditor : WidgetComponentConcept {
      PrimitiveEditor();
      ~PrimitiveEditor();

      struct Meta {
        Weak<ZsPrimitive> _scene;
      };

      void paint() override;

    protected:
      std::string _sceneContextLabel{""};
      TabEntries<Meta> _tabs;
    };

    /// details
    bool tree_node_get_open(TreeNodeBase *node);
    void tree_node_set_open(TreeNodeBase *node, bool open);
    int tree_close_and_unselect_child_nodes(TreeNodeBase *node,
                                            ImGuiSelectionBasicStorage *selection, int depth = 0);
    void tree_set_all_in_open_nodes(TreeNodeBase *node, ImGuiSelectionBasicStorage *selection,
                                    bool selected);
    TreeNodeBase *tree_get_next_node_in_visible_order(TreeNodeBase *curr_node,
                                                      TreeNodeBase *last_node);

  }  // namespace ui

}  // namespace zs