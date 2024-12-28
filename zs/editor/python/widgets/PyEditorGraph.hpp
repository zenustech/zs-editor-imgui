#pragma once
#include <Python.h>

#include "editor/widgets/GraphWidgetComponent.hpp"
#include "interface/details/PyHelper.hpp"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////

extern PyTypeObject ZpyEditorGraph_Type;
#define ZpyEditorGraph_Check(v) \
  (PyObject_IsInstance(static_cast<PyObject *>(v), static_cast<PyObject *>(&ZpyEditorGraph_Type)))

typedef struct {
  //
  PyObject_HEAD

      zs::ge::Graph *graph;
  int own;
} ZpyEditorGraph;

///////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif