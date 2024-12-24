#include "PyEval.hpp"
#include <memory>
#ifdef _DEBUG
#undef _DEBUG
#include <Python.h>
#define _DEBUG
#else
#include <Python.h>
#endif

namespace zs {

std::string python_evaluate(const std::string &script,
                            const std::vector<std::string> &args) {

  Py_Initialize();

  // pass arguments
  std::vector<wchar_t *> as;
  for (auto &&arg : args)
    as.push_back(Py_DecodeLocale(arg.c_str(), NULL));

  // throw std::runtime_error(
  //     "there exists an argument not of StringObject type!");
  PyObject *pyargs = PyList_New(as.size());
  for (int i = 0; i < as.size(); ++i)
    PyList_SetItem(pyargs, i, PyUnicode_FromWideChar(as[i], wcslen(as[i])));

  PyObject *sys = PyImport_ImportModule("sys");
  PyObject_SetAttrString(sys, "argv", pyargs);

  Py_DECREF(pyargs);
  for (auto a : as)
    PyMem_Free(a);

  PyRun_SimpleString(
      "import sys\n"
      "from io import StringIO\n"
      "old_stdout = sys.stdout\n" // Backup the original stdout
      "sys.stdout = StringIO()\n" // Replace stdout with a StringIO object
                                  // to capture outputs
  );

  PyRun_SimpleString(script.data());

  PyObject *stdOut = PyObject_GetAttrString(sys, "stdout");
  PyObject *output = PyObject_GetAttrString(stdOut, "getvalue");
  PyObject *result = PyObject_CallObject(output, NULL);

  // Convert the captured output to a C++ string
  const char *result_cstr = PyUnicode_AsUTF8(result);

  // Restore the original stdout
  PyRun_SimpleString("sys.stdout = old_stdout");

  // Finalize the Python interpreter
  Py_Finalize();

  return std::string(result_cstr);
}

}