#include "PyEditorGraph.hpp"

#include "editor/GuiWindow.hpp"
#include "editor/python/Init.hpp"
#include "editor/widgets/GraphWidgetComponent.hpp"
#include "interface/world/value_type/ValueInterface.hpp"
#include "world/core/Utils.hpp"

namespace detail {
  template <bool IsInput = true, typename ParentT = void *>
  static void setup_pin(ZsList list, ParentT parentPtr) {
    if (list.isList()) {
      for (ZsDict pinDesc : list) {
        if (pinDesc.isDict()) {
          ZsString nameStr = pinDesc["name"];

          /// type
          ZsString typeStr = pinDesc["type"];
          zs::ge::Pin *pin = nullptr;

          if constexpr (zs::is_same_v<ParentT, zs::ge::Node *>) {
            if constexpr (IsInput)
              pin = parentPtr->appendInput(typeStr.c_str());
            else
              pin = parentPtr->appendOutput(typeStr.c_str());
          } else
            pin = parentPtr->append(typeStr.c_str());

          pin->setupContentAndWidget(pinDesc);

          /// children
          ZsList childPins = pinDesc["children"];
          detail::setup_pin(childPins, pin);
        }
      }
    }
  }
}  // namespace detail

#ifdef __cplusplus
extern "C" {
#endif

///
/// modules class
///
PyDoc_STRVAR(EditorGraph_doc,
             "Editor graph class.\n"
             "\n"
             ".. method:: __init__()\n"
             "\n"
             "   Default constructor.");

/// @note tp_alloc and tp_free handle PyObject*
static PyObject *EditorGraph_New(PyTypeObject *type, PyObject *args_, PyObject *kwds) {
  ZpyEditorGraph *self;
  self = (ZpyEditorGraph *)type->tp_alloc(type, 0);
  if (self != NULL) {
    self->graph = nullptr;
    self->own = 0;

    ZsObject args = args_;
    assert(args.pytype() == &PyTuple_Type);
    {
      PyObject *arg;
      if (PyArg_ParseTuple(as_ptr_<PyObject>(args), "O", &arg)) {
        if (Py_TYPE(arg) == &PyCapsule_Type) {
          auto graphPtr = PyCapsule_GetPointer(arg, NULL);
          self->graph = static_cast<zs::ge::Graph *>(graphPtr);
          // self->own = false;
        } else {
          ZsValue label = arg;
          if (PyVar pybytes = zs_bytes_obj(label)) {
            self->graph = new zs::ge::Graph(pybytes.asBytes().c_str());
            self->own = 1;
            if (self->graph == NULL) {
              Py_DECREF(self);
              Py_DECREF(arg);
              return PyErr_NoMemory();
            }
          }
        }
        Py_DECREF(arg);
      } else {
        PyErr_Print();

        self->graph = new zs::ge::Graph("default");
        self->own = 1;
        if (self->graph == NULL) {
          Py_DECREF(self);
          return PyErr_NoMemory();
        }
      }
    }
    return (PyObject *)self;
  }
  Py_RETURN_NONE;
}

static int EditorGraph_init(ZpyEditorGraph *self, PyObject *args, PyObject *kwds) { return 0; }

static void EditorGraph_dealloc(ZpyEditorGraph *self) {
  if (self->own && self->graph) delete self->graph;
  Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *EditorGraph_repr(ZpyEditorGraph *self) {
  return PyUnicode_FromFormat("type: %s - graph c handle: %p", Py_TYPE(self)->tp_name, self->graph);
}

PyDoc_STRVAR(EditorGraph_load_doc,
             ".. method:: load(filename)\n"
             "\n"
             "   realod graph from json file given by [filename].");

static PyObject *EditorGraph_load(ZpyEditorGraph *self, PyObject *name) {
  if (false) {
    PyErr_SetString(PyExc_RuntimeError, "cannot decrement any more");
    return nullptr;
  }
  ZsValue value{name};
  if (PyVar pybytes = zs_bytes_obj(value)) {
    // g_zs_variable_apis.reflect(pybytes);
    if (auto cstr = pybytes.getValue().asBytes().c_str()) {
      self->graph->load(cstr);
      Py_RETURN_NONE;
    }
  }
  Py_RETURN_NONE;
}
PyDoc_STRVAR(EditorGraph_save_doc,
             ".. method:: save()\n"
             "\n"
             "   save graph to json file.");

static PyObject *EditorGraph_save(ZpyEditorGraph *self) {
  self->graph->save();
  Py_RETURN_NONE;
}
PyDoc_STRVAR(EditorGraph_addNode_doc,
             ".. method:: addNode(name)\n"
             "\n"
             "   add node given by name [filename].");

static PyObject *EditorGraph_addNode(ZpyEditorGraph *self, PyObject *name) {
  ZsValue value{name};
  if (PyVar pybytes = zs_bytes_obj(value)) {
    if (auto cstr = pybytes.asBytes().c_str()) {
      auto pos = self->graph->nextSuggestedPosition();
      auto [iter, success] = self->graph->spawnNode(cstr);
      auto nid = iter->first;
      auto &node = iter->second;
      auto ed = self->graph->getEditorContext();
      ed->SetNodePosition(nid, pos);
      // ed->ClearSelection();
      // ed->SelectObject(ed->FindNode(nid));
      // fmt::print("placing node [{}] at {}, {}\n", cstr, pos.x, pos.y);
      // fmt::print("currently {} nodes present.\n",
      // self->graph->_nodes.size()); for (auto &[id, node] :
      // self->graph->_nodes)
      //   fmt::print("checking node [{}] with name [{}]\n", id.Get(),
      //   node._name);
    }
  }
  Py_RETURN_NONE;
}

PyDoc_STRVAR(EditorGraph_addNodeByDesc_doc,
             ".. method:: addNodeByDesc(name)\n"
             "\n"
             "   add node given by name [filename].");

/*
g = zpy.ui.graph("Editor")
g.addNodeByDesc('123213', {"inputs" : [{"name" : "list_0", "type" : "list",
"defl" : "[23, 23, 11]"}, {"name" : "dict_1", "type" : "dict"}, {"name" :
"obj_2"}]})
g.addNodeByDesc('abcdefghijklmnopqrstuvwxyz', {"outputs" : [{"name" : "list_0",
"type" : "list", "defl" : "[23, 23, 11]"}, {"name" : "dict_1", "type" : "dict"},
{"name" : "obj_2"}]})

g.addNodeByDesc('abcdefghijklmnopqrstuvwxyz',
{"outputs" :
[
{"name" : "i0", "type" : "int", "defl" : "5"},
{"name" : "list_0","type" : "list", "defl" : "[23, 23, 11]"},
{"name" : "dict_1", "type" : "dict", "defl" : {'00' : 123, '11' : 'asdfas', '22'
: 3.14}},
{"name" : "obj_2", "type" : "enum aa bb ccc", "defl" : "ccc"},
{"name" : "obj_2", "type" : "str", "defl" : "some string"}, ],
"attribs" : {
"inherent" : [{"name" : "arg0", "type" : "int", "defl" : "0"},
{"name" : "arg1", "type" : "float", "defl" : "0.0"}], }
}
)
*/
static PyObject *EditorGraph_addNodeByDesc(ZpyEditorGraph *self, PyObject *args_) {
  ZsObject args = args_;
  PyObject *str_, *dict_;
  if (PyArg_ParseTuple(as_ptr_<PyObject>(args), "OO", &str_, &dict_)) {
    ZsDict dict = dict_;
    if (dict.isDict()) {
      if (PyVar pybytes = zs_bytes_obj(str_)) {
        if (auto cstr = pybytes.asBytes().c_str()) {
          auto pos = self->graph->nextSuggestedPosition();
          auto [iter, success] = self->graph->spawnNode(cstr);
          auto nid = iter->first;
          auto &node = iter->second;
          auto ed = self->graph->getEditorContext();
          ed->SetNodePosition(nid, pos);

          ZsList inputList = dict["inputs"];
          detail::setup_pin<true>(inputList, &node);

          ZsList outputList = dict["outputs"];
          detail::setup_pin<false>(outputList, &node);

          ZsDict attribDict = dict["attribs"];
          node.setupAttribWidgets(attribDict);
          if (attribDict) {
            zs_print_py_cstr(fmt::format("attribs of {} categories\n", attribDict.size()).c_str());
          }
          ZsList categoryList = dict["category"];
          if (categoryList) {
            zs_print_py_cstr(
                fmt::format("categoryList of {} elements\n", categoryList.size()).c_str());
          }
        }
      }
    }
    /// @note PyArg_ParseTuple gives you a borrowed reference!
    /// @note
    /// https://docs.python.org/3/extending/extending.html#extracting-parameters-in-extension-functions
    // Py_DECREF(str_);
    // Py_DECREF(dict_);
  } else {
    PyErr_SetString(PyExc_RuntimeError, "Requires a \'str\' and a \'dict\' as arguments");
    // zs_print_err_py_cstr("Requires a \'str\' and a \'dict\' as arguments");
  }
  Py_RETURN_NONE;
}

static PyMethodDef EditorGraph_methods[] = {
    {"load", (PyCFunction)EditorGraph_load, METH_O, EditorGraph_load_doc},
    {"save", (PyCFunction)EditorGraph_save, METH_NOARGS, EditorGraph_save_doc},
    {"addNode", (PyCFunction)EditorGraph_addNode, METH_O, EditorGraph_addNode_doc},
    {"addNodeByDesc", (PyCFunction)EditorGraph_addNodeByDesc, METH_VARARGS,
     EditorGraph_addNodeByDesc_doc},
    {nullptr, nullptr, 0, nullptr},
};

PyTypeObject ZpyEditorGraph_Type = {
    /*ob_base*/ PyVarObject_HEAD_INIT(nullptr, 0)
    /*tp_name*/ "EditorGraph",
    /*tp_basicsize*/ sizeof(ZpyEditorGraph),
    /*tp_itemsize*/ 0,
    /*tp_dealloc*/ (destructor)EditorGraph_dealloc,
    /*tp_vectorcall_offset*/ 0,
    /*tp_getattr*/ nullptr,
    /*tp_setattr*/ nullptr,
    /*tp_as_async*/ nullptr,
    /*tp_repr*/ (reprfunc)EditorGraph_repr,
    /*tp_as_number*/ nullptr,
    /*tp_as_sequence*/ nullptr,
    /*tp_as_mapping*/ nullptr,
    /*tp_hash*/ nullptr,
    /*tp_call*/ nullptr,
    /*tp_str*/ nullptr,
    /*tp_getattro*/ nullptr,
    /*tp_setattro*/ nullptr,
    /*tp_as_buffer*/ nullptr,
    /*tp_flags*/ Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    /*tp_doc*/ EditorGraph_doc,
    /*tp_traverse*/ nullptr,
    /*tp_clear*/ nullptr,
    /*tp_richcompare*/ nullptr,
    /*tp_weaklistoffset*/ 0,
    /*tp_iter*/ nullptr,
    /*tp_iternext*/ nullptr,
    /*tp_methods*/ EditorGraph_methods,
    /*tp_members*/ nullptr,
    /*tp_getset*/ nullptr,  //
    /*tp_base*/ nullptr,
    /*tp_dict*/ nullptr,
    /*tp_descr_get*/ nullptr,
    /*tp_descr_set*/ nullptr,
    /*tp_dictoffset*/ 0,
    /*tp_init*/ (initproc)EditorGraph_init,
    /*tp_alloc*/ nullptr,
    /*tp_new*/ EditorGraph_New,  // PyType_GenericNew
};

///
/// modules utils
///
static PyObject *get_current_editor_graphs(PyObject *self) {
  // pymap of graphs
  auto &graphs = zs::GUIWindow::s_activeGraphs();
  ZsDict map = zs_dict_obj_default();
  for (auto &&[label, graphPtr] : graphs) {
    if (ZsObject entry = PyCapsule_New(graphPtr.get(), NULL, NULL)) {
      if (PyVar args = Py_BuildValue("(O)", entry.handle())) {
        if (PyVar item = EditorGraph_New(&ZpyEditorGraph_Type, args, NULL)) {
          if (PyDict_SetItemString(as_ptr_<PyObject>(map), label.c_str(), item)) PyErr_Print();
        }
      } else {  // tuple pack error
        Py_DECREF(entry.handle());
        PyErr_Print();
      }
    } else  // capsule pack error
      PyErr_Print();
  }
  return as_ptr_<PyObject>(map);
}
static PyObject *get_editor_graph(PyObject *self, PyObject *label) {
  // pymap of graphs
  ZsValue v{label};
  if (PyVar pybytes = zs_bytes_obj(v)) {
    auto &graphs = zs::GUIWindow::s_activeGraphs();
    if (auto it = graphs.find(std::string(pybytes.asBytes().c_str())); it != graphs.end()) {
      if (ZsObject graphHandle = PyCapsule_New(it->second.get(), NULL, NULL)) {
        // fmt::print("its ptr is [{}] (ref: {})\n",
        //            PyCapsule_GetPointer(graphHandle, NULL),
        //            (void *)it->second.get());
        if (PyVar args = Py_BuildValue("(O)", as_ptr_<PyObject>(graphHandle))) {
          return EditorGraph_New(&ZpyEditorGraph_Type, args, NULL);
        } else {
          Py_DECREF(graphHandle.handle());
          PyErr_Print();
        }
      } else
        PyErr_Print();
    }
  }
  Py_RETURN_NONE;
}

static PyMethodDef zpy_ui_methods[] = {
    {"graphs", (PyCFunction)get_current_editor_graphs, METH_NOARGS, nullptr},
    {"graph", (PyCFunction)get_editor_graph, METH_O, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

int EditorGraph_Init(void *mod_) {
  PyObject *mod = static_cast<PyObject *>(mod_);
  if (mod == nullptr) return -1;

  if (PyType_Ready(&ZpyEditorGraph_Type) < 0) return -1;
  Py_INCREF(&ZpyEditorGraph_Type);
  PyModule_AddObject(mod, "EditorGraph", (PyObject *)&ZpyEditorGraph_Type);

  for (int i = 0; zpy_ui_methods[i].ml_name; ++i) {
    PyMethodDef *method = &zpy_ui_methods[i];
    PyModule_AddObject(mod, method->ml_name, (PyObject *)PyCFunction_New(method, nullptr));
  }
  return 0;
}

#ifdef __cplusplus
}
#endif
