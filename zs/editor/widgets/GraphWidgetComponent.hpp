#pragma once
#include <filesystem>
#include <list>
#include <optional>
#include <queue>
#include <set>
#include <string>

#include <json.hpp>

#include "editor/ImguiSystem.hpp"
#include "imgui.h"

#include "editor/widgets/ResourceWidgetComponent.hpp"

#include "imgui_node_editor_internal.h"

#include "imgui_node_editor.h"
#include "utilities/drawing.h"
#include "utilities/widgets.h"

#include "zensim/ZpcFunction.hpp"
#include "zensim/ZpcImplPattern.hpp"
#include "zensim/types/ImplPattern.hpp"
#include "zensim/ui/Widget.hpp"
#include "zensim/zpc_tpls/fmt/format.h"

#include "interface/world/ObjectInterface.hpp"

namespace zs {

namespace ed = ax::NodeEditor;

namespace ge { // graph editor

using ax::Widgets::IconType;

enum pin_type_e : u32 {
  Flow = 0, // 0
  Bool,     // 1
  Int,
  Float,
  String,
  List, // 5
  Dict,
  Object,
  Function, // 8
  Delegate, // 9
  Event,    // 10
};

enum pin_kind_e : u8 { Output, Input };

enum node_type_e : u32 { Blueprint, Simple, Tree, Comment, Houdini };

struct Link;
struct Pin;
using PinList = std::list<Pin>;
struct Node;
struct Graph;

struct PinConcept;
struct NodeConcept;
struct GraphConcept;

struct PinConcept {
  ~PinConcept() = default;
  virtual void draw(NodeConcept *) = 0;
  virtual PinConcept *parent() { return nullptr; }
};
struct NodeConcept {
  ~NodeConcept() = default;
  virtual void draw(GraphConcept *) = 0;
  virtual float width() = 0;
  virtual float height() = 0;
};
struct GraphConcept {
  ~GraphConcept() = default;
  virtual void draw() = 0;
};

struct Pin {
  static float get_pin_icon_size() noexcept {
    return ImguiSystem::get_font_size() * ImGui::GetIO().FontGlobalScale;
  }
  static float get_min_content_size() noexcept {
    // return get_pin_icon_size() * 3;
    return get_pin_icon_size();
  }
  static float get_half_insertion_zone_span() noexcept {
    return get_pin_icon_size() * 0.4f;
  }
  IconType getIconType() const;
  ImColor getIconColor() const;

  // might be hierarchical
  ed::PinId _id;
  ge::Node *_node;
  std::string _name;
  std::optional<bool> _expanded;
  std::optional<bool> _labelInEdit;
  pin_type_e _type;
  pin_kind_e _kind;
  std::set<Link *> _links;
  PinList _chs;
  Pin *_parent;

  ZsVar _contents;
  zs::ui::GenericResourceWidget _contentWidget;

  int getPinIndex() const;

  int _curChildPinNo;

  bool _visible;

  Pin(ed::PinId id, Node *node, std::string_view name, pin_type_e type,
      pin_kind_e _kind = pin_kind_e::Input, Pin *parent = nullptr)
      : _id{id}, _node{node}, _name{name}, _expanded{}, _labelInEdit{false},
        _type{type}, _kind{_kind}, _parent{parent}, _contents{},
        _contentWidget{}, _visible{true} {}
  ~Pin(); // unregister pin and delete related links

  Pin(Pin &&o) noexcept
      : _id{zs::exchange(o._id, ed::PinId{0})},
        _node{zs::exchange(o._node, nullptr)}, _name{zs::move(o._name)},
        _expanded{zs::move(o._expanded)}, _type{o._type}, _kind{o._kind},
        _links{zs::move(o._links)}, _chs{zs::move(o._chs)},
        _parent{zs::exchange(o._parent, nullptr)},
        _contents{zs::move(o._contents)},
        _contentWidget{zs::move(o._contentWidget)},
        _visible{zs::exchange(o._visible, false)} {}

  friend void swap(Pin &a, Pin &b) noexcept;
  Pin &operator=(Pin &&o) noexcept {
    swap(*this, o);
    return *this;
  }

  bool removeChildPin(const Pin *pin);

  bool visible() const noexcept { return _visible; }
  void setVisible() noexcept { _visible = true; }
  void setInvisible() noexcept { _visible = false; }

  [[maybe_unused]] Pin *prepend(std::string_view label);
  [[maybe_unused]] Pin *prepend(const ed::PinId &id, std::string_view label);

  [[maybe_unused]] Pin *append(std::string_view label);
  [[maybe_unused]] Pin *append(const ed::PinId &id, std::string_view label);
  [[maybe_unused]] Pin *append(const Pin *target, std::string_view label);
  [[maybe_unused]] Pin *append(const Pin *target, const ed::PinId &id,
                               std::string_view label);
  void remove_back() {
    if (!_chs.empty())
      _chs.pop_back();
  }

  void drawLabelText();
  void paint(float width = 0.f);

  /// @note state query
  bool expanded() const noexcept {
    if (_expanded)
      return _expanded.value();
    return false;
  }
  bool expandable() const noexcept { return _expanded.has_value(); }
  bool hasContents() const noexcept { return _contentWidget; }
  bool editable(bool connected = false) const noexcept {
    return !connected && hasContents();
  }

  ZsVar &contents() { return _contents; }
  const ZsVar &contents() const { return _contents; }
  zs::ui::GenericResourceWidget &setupContentWidget(ZsValue aux = {});
  void drawContentItem() { _contentWidget.draw(); }
  void setupContentAndWidget(ZsDict desc);

  float evaluatePinWidth(float offset = 0.f) const;
  float evaluateIconTextVerticalOffset() const {
    return (get_pin_icon_size() - ImGui::CalcTextSize(_name.c_str()).y) * 0.5f;
  }
  void removeLink(Link *link) { auto cnt = _links.erase(link); }
  bool isPinLinked() const;
  ed::PinKind getEditorPinKind() const noexcept {
    if (_kind == pin_kind_e::Output)
      return ed::PinKind::Output;
    else
      return ed::PinKind::Input;
  }
};

struct Node {
  ed::NodeId _id;
  std::string _name;
  Graph *_graph;
  Weak<Graph> _subgraph;
  PinList _inputs;
  PinList _outputs;
  ImVec2 _pos;

  node_type_e _type;

  int _curChildPinNo;
  float _nodeWidth, _nodeBound;

  std::map<std::string, ZsVar> _attribs;
  zs::ui::GenericAttribWidget _attribWidget;

  std::string cmdText, helperText;

  Node(Node &&o) noexcept
      : _id{zs::exchange(o._id, ed::NodeId{0})}, _name{zs::move(o._name)},
        _graph{zs::exchange(o._graph, nullptr)},
        _subgraph{zs::move(o._subgraph)}, _inputs{zs::move(o._inputs)},
        _outputs{zs::move(o._outputs)}, _pos{o._pos}, _type{o._type},
        _nodeWidth{o._nodeWidth}, _nodeBound{o._nodeBound},
        cmdText{zs::move(o.cmdText)}, helperText{zs::move(o.helperText)},
        _attribs{zs::move(o._attribs)},
        _attribWidget{zs::move(o._attribWidget)} {}
  friend void swap(Node &a, Node &b) noexcept;
  Node &operator=(Node &&o) noexcept {
    swap(*this, o);
    return *this;
  }

  Node(ed::NodeId id, Graph *graph, std::string_view name,
       ImVec2 pos = ImVec2(0, 0))
      : _id{id}, _graph{graph}, _subgraph{}, _name{name}, _pos{pos},
        _type{node_type_e::Blueprint}, cmdText((std::size_t)128, '\0'),
        _attribs{}, _attribWidget{} {}
  ~Node();

  Pin *inputPin(int k) {
    for (auto &input : _inputs) {
      if (k == 0)
        return &input;
      else if (k < 0)
        break;
      k--;
    }
    return nullptr;
  }
  [[maybe_unused]] Pin *appendInput(std::string_view label);
  [[maybe_unused]] Pin *appendOutput(std::string_view label);
  [[maybe_unused]] Pin *appendInput(const ed::PinId &id,
                                    std::string_view label);
  [[maybe_unused]] Pin *appendOutput(const ed::PinId &id,
                                     std::string_view label);

  [[maybe_unused]] Pin *appendInput(const Pin *target, std::string_view label);
  [[maybe_unused]] Pin *appendOutput(const Pin *target, std::string_view label);
  [[maybe_unused]] Pin *appendInput(const Pin *target, const ed::PinId &id,
                                    std::string_view label);
  [[maybe_unused]] Pin *appendOutput(const Pin *target, const ed::PinId &id,
                                     std::string_view label);

  // void clearInput(std::string_view label);
  // void clearOutput(std::string_view label);
  void clearInputs();
  void clearOutputs();

  Pin *findInputPin(std::string_view tag) {
    for (auto &input : _inputs)
      if (input._name == tag)
        return &input;
    return nullptr;
  }
  Pin *findOutputPin(std::string_view tag) {
    for (auto &output : _outputs)
      if (output._name == tag)
        return &output;
    return nullptr;
  }
  Pin *findPin(std::string_view tag) {
    if (auto p = findInputPin(tag))
      return p;
    else if (auto p = findOutputPin(tag))
      return p;
    return nullptr;
  }
  bool removeInputPin(const Pin *pin) {
    for (auto it = std::begin(_inputs); it != std::end(_inputs); ++it) {
      if (zs::addressof(*it) == pin) {
        _inputs.erase(it);
        return true;
      } else if ((*it).removeChildPin(pin)) {
        return true;
      }
    }
    return false;
  }
  bool removeOutputPin(const Pin *pin) {
    for (auto it = std::begin(_outputs); it != std::end(_outputs); ++it) {
      if (zs::addressof(*it) == pin) {
        _outputs.erase(it);
        return true;
      } else if ((*it).removeChildPin(pin)) {
        return true;
      }
    }
    return false;
  }
  bool removePin(const Pin *pin) {
    if (removeInputPin(pin))
      return true;
    else if (removeOutputPin(pin))
      return true;
    return false;
  }

  auto &attribs() { return _attribs; }
  const auto &attribs() const { return _attribs; }
  ZsVar &setupAttribAndWidget(std::string_view label, ZsDict desc);
  zs::ui::GenericAttribWidget &setupAttribWidgets(ZsDict desc);
  void drawAttribItems() { _attribWidget.draw(); }

  float evaluateInputPinWidth() const;
  float evaluateOutputPinWidth() const;

  void paint();
};

struct Link {
  ed::LinkId _id;
  ed::PinId _startPinID, _endPinID;
  Pin *_srcPin, *_dstPin;

  Link(ed::LinkId id, Pin *src, Pin *dst)
      : _id{id}, _startPinID{src->_id}, _endPinID{dst->_id}, _srcPin{src},
        _dstPin{dst} {
    src->_links.insert(this);
    dst->_links.insert(this);
  }
  Link(Link &&o) noexcept
      : _id{zs::exchange(o._id, ed::LinkId{0})},
        _startPinID{zs::exchange(o._startPinID, ed::PinId{0})},
        _endPinID{zs::exchange(o._endPinID, ed::PinId{0})},
        _srcPin{zs::exchange(o._srcPin, nullptr)},
        _dstPin{zs::exchange(o._dstPin, nullptr)} {}
  friend void swap(Link &a, Link &b) noexcept {
    zs_swap(a._id, b._id);
    zs_swap(a._startPinID, b._startPinID);
    zs_swap(a._endPinID, b._endPinID);
    zs_swap(a._srcPin, b._srcPin);
    zs_swap(a._dstPin, b._dstPin);
  }
  Link &operator=(Link &&o) noexcept {
    swap(*this, o);
    return *this;
  }
  ~Link();

  void paint();
};

struct Graph {
  struct EditorContextGuard {
    EditorContextGuard(ed::EditorContext *ctx) {
      _prevCtx = ed::GetCurrentEditor();
      ed::SetCurrentEditor(ctx);
    }
    ~EditorContextGuard() { ed::SetCurrentEditor(_prevCtx); }

  private:
    ed::EditorContext *_prevCtx;
  };
  EditorContextGuard contextGuard() { return {_edCtx}; }

  struct PinComp {
    bool operator()(const ed::PinId &l, const ed::PinId &r) const noexcept {
      return l.AsPointer() < r.AsPointer();
    }
  };
  struct NodeComp {
    bool operator()(const ed::NodeId &l, const ed::NodeId &r) const noexcept {
      return l.AsPointer() < r.AsPointer();
    }
  };
  struct LinkComp {
    bool operator()(const ed::LinkId &l, const ed::LinkId &r) const noexcept {
      return l.AsPointer() < r.AsPointer();
    }
  };

  using PinIdInt = decltype(declval<ed::PinId>().Get());
  using PinMap = std::map<ed::PinId, Pin *, PinComp>;
  using NodeMap = std::map<ed::NodeId, Node, NodeComp>;
  using LinkMap = std::map<ed::LinkId, Link, LinkComp>;

  ed::NodeId nextNodeId() { return ed::NodeId(_nextObjectId++); }
  ed::PinId nextPinId() { return ed::PinId(_nextObjectId++); }
  ed::LinkId nextLinkId() { return ed::LinkId(_nextObjectId++); }
  void updateObjectId(u32 id) {
    if (id > _nextObjectId)
      _nextObjectId = id + 1;
    else if (id == _nextObjectId)
      _nextObjectId++;
  }

  u32 _nextObjectId;
  std::string _name; // same with GraphWidgetComponent
  std::string _fileName;
  ed::EditorContext *_edCtx; // editor associated with [name]
  /// @note cleared upon Pin dtor (within node dtor)
  /// @note link ptr cleared upon Link dtor
  LinkMap _links;
  PinMap _pins; // owned by _nodes
  NodeMap _nodes;

  std::string _viewJson;
  ImRect _viewRect;
  bool _initRequired;

  /// actions
  std::map<ImGuiID, zs::function<void()>> _auxiliaryWidgetActions;
  std::queue<zs::function<void()>> _itemMaintenanceActions;
  std::set<std::pair<PinIdInt, PinIdInt>> _drawnLinks;

  /// for interactions with other gui widgets
  ImGuiID _prevAltOwner = 0;
  ed::NodeId hoveredNode;
  ed::PinId hoveredPin;
  ed::LinkId hoveredLink;
  std::vector<Node *> selectedNodes;
  unsigned long long numSelectedNodes;
  bool selectionChanged;

  Graph(Graph &&o) noexcept
      : _nextObjectId{zs::exchange(o._nextObjectId, (u32)0)},
        _name{zs::move(o._name)}, _fileName{zs::move(o._fileName)},
        _edCtx{zs::exchange(o._edCtx, nullptr)}, _links{zs::move(o._links)},
        _pins{zs::move(o._pins)}, _nodes{zs::move(o._nodes)},
        _viewJson{zs::move(o._viewJson)}, _viewRect{zs::move(o._viewRect)},
        _initRequired{zs::exchange(o._initRequired, false)} {
    updateGraphLinks();
  }

  friend void swap(Graph &a, Graph &b) noexcept;

  Graph &operator=(Graph &&o) noexcept {
    swap(*this, o);
    return *this;
  }

  Graph(std::string_view name, std::string_view filename)
      : _nextObjectId{1}, _name{name}, _fileName{filename},
        _initRequired{false}, _viewJson{} {
    hoveredNode = 0;
    hoveredPin = 0;
    hoveredLink = 0;
    numSelectedNodes = 0;
    selectionChanged = false;

    acquireEditorContext(_name);
    init(_fileName);
  }
  Graph(std::string_view name)
      : Graph(name, fmt::format("tdg_{}.json", name)) {}

  ~Graph() {
    if (_edCtx) {
      _nextObjectId = 1;
      {
        auto guard = contextGuard();
        // auto curEditor = ed::GetCurrentEditor();
        // ed::SetCurrentEditor(_edCtx);
        _links.clear();
        _pins.clear();
        _nodes.clear();
        // ed::SetCurrentEditor(curEditor);
      }
      _edCtx = nullptr;
      ImguiSystem::erase_node_editor(_name);
    }
  }
  void reset() { operator=(Graph{}); }

  ed::Detail::EditorContext *getEditorContext() const {
    return reinterpret_cast<ed::Detail::EditorContext *>(
        const_cast<ed::EditorContext *>(_edCtx));
  }

  Node *spawnDummyNode();

  ed::NodeId lastNodeId() const {
    if (!_nodes.empty())
      return (*std::prev(std::end(_nodes))).first;
    // return (*++std::rbegin(_nodes)).first;
    return ed::NodeId();
  }
  ImVec2 nextSuggestedPosition() const {
    auto ctx = getEditorContext();
    if (ctx && !_nodes.empty())
      return getEditorContext()->GetNodePosition(lastNodeId()) + ImVec2(10, 10);
    return ImVec2(0, 0);
  }

  /// maintenanceActions are event-driven actions
  template <typename F> void enqueueMaintenanceAction(F &&f) {
    _itemMaintenanceActions.emplace(zs::move(f));
  }

  /// auxiliaryWidgetActions for persistant popups
  template <typename F> void setupAuxiliaryWidgetAction(ImGuiID id, F &&f) {
    _auxiliaryWidgetActions.emplace(id, zs::move(f));
  }
  void clearAuxiliaryWidgetAction(ImGuiID id) {
    _auxiliaryWidgetActions.erase(id);
  }

  zs::tuple<NodeMap::iterator, bool> spawnNode(std::string_view name,
                                               ed::NodeId nid);
  zs::tuple<NodeMap::iterator, bool> spawnNode(std::string_view name);
  zs::tuple<LinkMap::iterator, bool> spawnLink(Pin *srcPin, Pin *dstPin);

  void paint();
  void save();
  void load();
  void load(std::string_view filename);
  void init(std::string_view filename);

  friend struct Pin;
  friend struct Node;

  Node *findNode(ed::NodeId id) {
    if (auto it = _nodes.find(id); it != _nodes.end())
      return zs::addressof(it->second);
    return nullptr;
  }
  Node *findNode(std::string_view tag) {
    for (auto &[id, node] : _nodes)
      if (node._name == tag)
        return &node;
    return nullptr;
  }
  Pin *findPin(ed::PinId id) {
    if (auto it = _pins.find(id); it != _pins.end())
      return it->second;
    return nullptr;
  }
  const Pin *findPin(ed::PinId id) const {
    if (auto it = _pins.find(id); it != _pins.end())
      return it->second;
    return nullptr;
  }

protected:
  Graph() noexcept
      : _nextObjectId{1}, _edCtx{nullptr}, _initRequired{false}, _viewJson{} {}

  bool hasSelectionChanged();
  const std::vector<Node *> &querySelectionNodes();

  void updateGraphLinks();
  void acquireEditorContext(std::string_view name);

  bool isLinked(ed::PinId id0, ed::PinId id1) const;
  void removePinLinks(Pin &pin);
  void removePinLinks(ed::PinId id);
};

} // namespace ge

struct GraphWidgetComponent : WidgetComponentConcept {
  GraphWidgetComponent() = default;
  GraphWidgetComponent(const Shared<ge::Graph> &graph) : _graph{graph} {}
  ~GraphWidgetComponent() = default;
  GraphWidgetComponent(GraphWidgetComponent &&) = default;
  GraphWidgetComponent &operator=(GraphWidgetComponent &&) = default;

  void paint() override {
    auto g = _graph.lock();
    {
      auto guard = g->contextGuard();

#if 0
    ed::Begin(g->_name.c_str());
    /// placeholder
    int id = 1;
    ed::BeginNode(id++);
    ImGui::Text("ZPC NODE");
    ed::EndNode();
    ed::End();
#else
      g->paint();
#endif
    }
  }

  std::string _label;
  Weak<ge::Graph> _graph;
};

} // namespace zs