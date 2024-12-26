#include "GraphWidgetComponent.hpp"

#include <filesystem>
#include <fstream>

#include "WidgetDrawUtilities.hpp"
// #include "utilities/builders.h"
#include "interface/details/PyHelper.hpp"
#include "interface/world/value_type/ValueInterface.hpp"
#include "json.hpp"
#include "zensim/io/Filesystem.hpp"
#include "zensim/zpc_tpls/fmt/color.h"

namespace zs {

  namespace ge {

    namespace fs = std::filesystem;
    using Json = nlohmann::json;

    Node *Graph::spawnDummyNode() {
      auto id = nextNodeId();
      fmt::print("spawning node [{}]!\n", id.Get());
      auto [iter, success] = _nodes.emplace(
          std::piecewise_construct, std::forward_as_tuple(id),
          std::forward_as_tuple(id, this, std::string("node") + std::to_string(id.Get())));
      if (success) {
        auto &node = iter->second;
        node.appendInput("i0");
        node.appendInput("input1");
        node.appendOutput("output0");
        node.appendOutput("o1");
      }
      return success ? zs::addressof(iter->second) : nullptr;
    }

    static void savePinAttribs(Json &entryJson, const Pin &pin) {
      if (pin.expandable()) entryJson["expansion"] = (int)pin.expanded();
      if (pin.hasContents()) {
        {
          GILGuard guard;
          const ZsVar &contents = pin.contents();
          PyVar str = zs_string_obj(contents);
          if (PyVar bs = zs_bytes_obj(str.handle())) {
            auto s = std::string(bs.asBytes().c_str());
            // fmt::print("saving pin {}\n", s);
            std::string transformedStr;
            transformedStr.reserve(s.size());
            for (auto c : s)
              if (c == '\"') transformedStr += "\\\"";
              // else if (c == '\'')
              //   transformedStr += "\'";
              else
                transformedStr += c;

            if (contents.getValue().isString())
              entryJson["content"] = fmt::format("\\\"{}\\\"", transformedStr);
            else
              // entryJson["content"] = s.substr(1, s.size() - 1);
              entryJson["content"] = transformedStr;
            // fmt::print("done saving pin {}\n", transformedStr);
          }
        }
      }
    }
    static void savePin(Json &pinJson, const PinList &pinList) {
      std::vector<Json> pinJsons(pinList.size());
      size_t i = 0;
      for (const auto &pin : pinList) {
        auto &pinJson = pinJsons[i++];
        auto &entryJson = pinJson[pin._name];
        savePin(entryJson["children"], pin._chs);
        entryJson["type"] = (int)pin._type;
        savePinAttribs(entryJson, pin);
      }
      pinJson = pinJsons;
    }

    Graph::Graph(std::string_view name)
        : Graph(name,
                fmt::format("{}/resource/graphs/___zs_graph_{}.json", abs_exe_directory(), name)) {}

    void Graph::save() {
      auto guard = contextGuard();

      auto editor = getEditorContext();  // detailed version

      Json json;
      /// "nodes"
      Json &nodesJson = json["nodes"];
      for (const auto &[id, node] : _nodes) {
        auto idStr = std::to_string(id.Get());
        auto &nodeJson = nodesJson[idStr];
        {
          auto pos = ed::GetNodePosition(node._id);
          auto &loc = nodeJson["uipos"];
          loc = std::array<float, 2>{pos.x, pos.y};
          nodeJson["type"] = (int)node._type;
          nodeJson["name"] = node._name;
        }
        { savePin(nodeJson["inputs"], node._inputs); }
        { savePin(nodeJson["outputs"], node._outputs); }
      }
      {
        /// links
        std::vector<Json> linkJsons(_links.size());
        size_t i = 0;
        auto assemblePinPath = [](Pin *pin) -> std::string {
          std::string p = pin->_name;
          do {
            auto par = pin->_parent;
            if (!par) {
              p = std::to_string(pin->_node->_id.Get()) + "/" + p;
              break;
            } else
              p = par->_name + "/" + p;
            pin = par;
          } while (true);
          return p;
        };
        for (auto &[id, link] : _links) {
          auto &linkJson = linkJsons[i++];
          auto &srcPin = link._srcPin;
          auto &dstPin = link._dstPin;
          // linkJson["src_node"] = srcPin->_node->_name;
          // linkJson["src_pin"] = srcPin->_name;
          linkJson["src_pin_path"] = assemblePinPath(srcPin);
          // linkJson["dst_node"] = dstPin->_node->_name;
          // linkJson["dst_pin"] = dstPin->_name;
          linkJson["dst_pin_path"] = assemblePinPath(dstPin);
        }
        json["links"] = linkJsons;
      }
      {
        /// view
        Json &viewJson = json["view"];
        const auto &canvasView = editor->GetView();
        viewJson["scroll"] = std::array<float, 2>{canvasView.Origin.x, canvasView.Origin.y};

        auto &rectJson = viewJson["visible_rect"];
        const auto &viewRect = editor->GetViewRect();
        rectJson["min"] = std::array<float, 2>{viewRect.Min.x, viewRect.Min.y};
        rectJson["max"] = std::array<float, 2>{viewRect.Max.x, viewRect.Max.y};
        viewJson["zoom"] = canvasView.Scale;
      }

      /// write
      fs::path filePath(_fileName);
      if (filePath.has_parent_path()) {
        if (!fs::exists(filePath.parent_path())) {
          if (!fs::create_directory(filePath.parent_path()))
            throw std::runtime_error(fmt::format("Unable to create folder [{}] for graph saving.\n",
                                                 filePath.parent_path().string()));
        }
      } else {
        throw std::runtime_error(
            fmt::format("Invalid file path [{}] for graph saving.\n", _fileName));
      }
      std::ofstream file(_fileName);
      if (file.is_open()) {
        // dump(4) prints the JSON data with an indentation of 4 spaces
        file << json.dump(4);
        file.close();
      } else {
        throw std::runtime_error(
            fmt::format("Could not open file [{}] for graph saving.\n", _fileName));
      }
    }

    static Pin *locatePin(const std::string &p, Graph &graph,
                          pin_kind_e pinKind = pin_kind_e::Input) {
      using Ti = RM_CVREF_T(std::string::npos);
      Ti st = 0;
      auto findNextLabel = [&p](Ti &st) -> std::string {
        auto ed = p.find_first_of("/\\", st + 1);
        auto str = p.substr(st, ed - st);
        st = p.find_first_not_of("/\\", ed);
        // fmt::print("\textracted path label {}\n", str);
        return str;
      };
      Pin *ret = nullptr;
      Node *node = graph.findNode(std::stoul(findNextLabel(st)));
      PinList *candidates = pinKind == pin_kind_e::Input ? &node->_inputs : &node->_outputs;
      bool found = false;
      do {
        found = false;
        auto pinLabel = findNextLabel(st);
        for (auto &pin : *candidates)
          if (pin._name == pinLabel) {
            ret = &pin;
            candidates = &pin._chs;
            found = true;
            break;
          }
        if (!found) return nullptr;
      } while (st != std::string::npos);
      return ret;
    }

    static void loadPinAttribs(const Json &pinAttribs, Pin *pin) {
      pin->_type = (pin_type_e)pinAttribs["type"].get<int>();
      if (auto it = pinAttribs.find("expansion"); it != pinAttribs.end()) {
        if ((*it).is_number_integer()) pin->_expanded = static_cast<int>(*it);
      }
      if (auto it = pinAttribs.find("content"); it != pinAttribs.end())
        if ((*it).is_string()) {
          auto str = static_cast<std::string>(*it);
          /// check if it is "enum ...", which is a combo
          pin->contents() = zs_eval_expr(str.c_str());
          // fmt::print("loading pin content: [{}]\n", str);
          if (pin->contents()) pin->setupContentWidget();
        }
    }
    static void loadChildPins(const Json &pinsJson, Pin *parent) {
      if (pinsJson.is_array()) {
        for (const auto &pinJson : pinsJson) {
          for (const auto &[pinName, pinAttribs] : pinJson.items()) {
            auto pin = parent->append(pinName);
            loadPinAttribs(pinAttribs, pin);
            loadChildPins(pinAttribs["children"], pin);
          }
        }
      }
    }
    void Graph::init(std::string_view filename) {
      if (_nextObjectId != 1) {
        fmt::print("graph already initialized.\n");
        return;
      }

      _fileName = filename;

      std::ifstream file(_fileName);
      Json json;
      if (file.is_open()) {
        // dump(4) prints the JSON data with an indentation of 4 spaces
        file >> json;
        file.close();
      } else {
        fmt::print("Could not open file [{}] for graph initialization.\n", _fileName);
        return;
      }

      {
        auto guard = contextGuard();

        auto editor = getEditorContext();  // detailed version

        /// nodes
        Json &nodesJson = json["nodes"];
        for (auto &[idStr, nodeAttribs] : nodesJson.items()) {
          auto idNo = std::stoul(idStr);
          auto nodeName = nodeAttribs["name"].get<std::string>();
          auto [iter, success] = spawnNode(nodeName, ed::NodeId(idNo));
          auto nid = iter->first;
          auto &node = iter->second;

          auto loc = nodeAttribs["uipos"].get<std::array<float, 2>>();
          node._pos = ImVec2(loc[0], loc[1]);
          // ed::SetNodePosition(nid, node._pos);

          auto &inputsJson = nodeAttribs["inputs"];
          if (inputsJson.is_array()) {
            auto &node = iter->second;
            for (const auto &inputJson : inputsJson) {
              for (const auto &[inputName, inputAttribs] : inputJson.items()) {
                // pinIds[inputName] = pid; // *
                auto pin = node.appendInput(inputName);
                loadPinAttribs(inputAttribs, pin);
                loadChildPins(inputAttribs["children"], pin);
              }
            }
          }
          auto &outputsJson = nodeAttribs["outputs"];
          if (outputsJson.is_array()) {
            for (const auto &outputJson : outputsJson) {
              for (const auto &[outputName, outputAttribs] : outputJson.items()) {
                auto pin = node.appendOutput(outputName);
                loadPinAttribs(outputAttribs, pin);
                loadChildPins(outputAttribs["children"], pin);
              }
            }
          }
        }
        /// links
        Json &linksJson = json["links"];
        for (auto &linkJson : linksJson) {
#if 0
      auto srcNodeTag = linkJson["src_node"].get<std::string>();
      auto srcPinTag = linkJson["src_pin"].get<std::string>();
      auto srcNodePtr = findNode(srcNodeTag);

      auto dstNodeTag = linkJson["dst_node"].get<std::string>();
      auto dstPinTag = linkJson["dst_pin"].get<std::string>();
      auto dstNodePtr = findNode(dstNodeTag);

      // fmt::print("attemping to link [{}, {}] to [{}, {}]\n", srcNodeTag,
      //            srcPinTag, dstNodeTag, dstPinTag);

      auto srcPinPtr = srcNodePtr->findPin(srcPinTag);
      auto dstPinPtr = dstNodePtr->findPin(dstPinTag);
#else
          // fmt::print("link [{}] to [{}]\n",
          // linkJson["src_pin_path"].get<std::string>(),
          // linkJson["dst_pin_path"].get<std::string>());
          auto srcPinPtr
              = locatePin(linkJson["src_pin_path"].get<std::string>(), *this, pin_kind_e::Output);
          auto dstPinPtr
              = locatePin(linkJson["dst_pin_path"].get<std::string>(), *this, pin_kind_e::Input);
#endif
          if (srcPinPtr && dstPinPtr) auto [iter, success] = spawnLink(srcPinPtr, dstPinPtr);
          // ed::Link(iter->first, iter->second._startPinID,
          // iter->second._endPinID);
        }
        /// view
        Json &viewJson = json["view"];
        ImVec2 viewOrigin{(float)viewJson["scroll"][0], (float)viewJson["scroll"][1]};
        float viewScale = (float)viewJson["zoom"];
        const auto &rectJson = viewJson["visible_rect"];
        ImVec2 viewRectMin{(float)rectJson["min"][0], (float)rectJson["min"][1]};
        ImVec2 viewRectMax{(float)rectJson["max"][0], (float)rectJson["max"][1]};
        _viewRect = ImRect{viewRectMin, viewRectMax};
#if 0
    auto &canvasView = const_cast<ImGuiEx::CanvasView &>(editor->GetView());
    canvasView.Set(-viewOrigin, viewScale);
    // fmt::print(fg(fmt::color::cyan), "BEGIN\n");
    editor->NavigateTo(_viewRect, false);
#endif
        {
          Json json;
          Json originJson;
          originJson["x"] = -viewOrigin[0];
          originJson["y"] = -viewOrigin[1];
          json["view"]["scroll"] = originJson;
          json["view"]["zoom"] = viewScale;
          auto &viewRectJson = json["view"]["visible_rect"];
          viewRectJson["max"]["x"] = viewRectMax.x;
          viewRectJson["max"]["y"] = viewRectMax.y;
          viewRectJson["min"]["x"] = viewRectMin.x;
          viewRectJson["min"]["y"] = viewRectMin.y;
          json["view"]["zoom"] = viewScale;
          _viewJson = json.dump(4);
        }
      }

      _initRequired = true;
    }

    void Graph::load() { load(_fileName); }
    void Graph::load(std::string_view filename) {
      _fileName = filename;

      std::ifstream file(_fileName);
      Json json;  // = Json::parse(file);
      if (file.is_open()) {
        // dump(4) prints the JSON data with an indentation of 4 spaces
        file >> json;
        file.close();
      } else {
        fmt::print("Could not open file [{}] for graph loading.\n", _fileName);
        return;
      }

      auto name = _name;
      auto tmpTag = _name + "__";
      {
        // use swap idiom because only replace after being successfully loaded
        ImguiSystem::erase_node_editor(tmpTag);

        Graph newGraph(tmpTag, _fileName);
        *this = zs::move(newGraph);
        // swap(*this, newGraph);
      }
      ImguiSystem::rename_node_editor(_name, name);
      acquireEditorContext(name);
    }

    void Graph::updateGraphLinks() {
      for (auto &[id, node] : _nodes) node._graph = this;
      if (auto ctx = getEditorContext())
        const_cast<ed::Detail::Config &>(ctx->GetConfig()).UserPointer = this;
    }

    void Graph::acquireEditorContext(std::string_view name) {
      if (!_name.empty() && _name != name && ImguiSystem::has_node_editor(_name))
        ImguiSystem::erase_node_editor(_name);

      _name = name;
      if (ImguiSystem::has_node_editor(_name)) {
        _edCtx = (ed::EditorContext *)ImguiSystem::get_node_editor(_name, nullptr);
        const_cast<ed::Detail::Config &>(getEditorContext()->GetConfig()).UserPointer = this;
      } else {
        ed::Config config;
        config.UserPointer = this;
        // config.CanvasSizeMode = ed::CanvasSizeMode::CenterOnly;
        /// called upon first ed::Begin(_name.c_str());
        config.LoadSettings = [](char *data, void *userPointer) -> size_t {
          auto graph = static_cast<Graph *>(userPointer);
          if (data == nullptr)
            return graph->_viewJson.size();
          else {
            memcpy(data, graph->_viewJson.data(), graph->_viewJson.size());
            return graph->_viewJson.size();
          }
        };
        config.SaveSettings = [](const char *data, size_t size, ed::SaveReasonFlags reason,
                                 void *userPointer) -> bool { return false; };
        _edCtx = (ed::EditorContext *)ImguiSystem::get_node_editor(_name, &config);
      }
    }

    bool Graph::hasSelectionChanged() { return ed::HasSelectionChanged(); }
    const std::vector<Node *> &Graph::querySelectionNodes() {
      std::vector<ed::NodeId> res(ed::GetSelectedObjectCount());
      auto n = ed::GetSelectedNodes(res.data(), ed::GetSelectedObjectCount());
      selectedNodes.resize(n);
      for (int i = 0; i != n; ++i) selectedNodes[i] = &_nodes.at(res[i]);
      return selectedNodes;
    }

  }  // namespace ge

}  // namespace zs
