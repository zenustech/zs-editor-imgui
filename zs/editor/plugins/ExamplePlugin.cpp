#include "interface/world/NodeInterface.hpp"
#include "interface/world/value_type/ValueInterface.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/zpc_tpls/fmt/color.h"

struct Tagged {
  Tagged() { fmt::print("ExampleNode tag created.\n"); }
  ~Tagged() { fmt::print("ExampleNode tag destroyed.\n"); }
  Tagged(Tagged &&) noexcept = default;
  Tagged(const Tagged &) noexcept = default;
};

#if 0
ZsObject create_pin_helper(bool expandable, bool ) {
  ;
  // return a dict {name, content type, expandable, removable, }
}
#endif

class ExampleNode : public zs::NodeConcept, public zs::NodeInterface<ExampleNode> {
public:
  ~ExampleNode() override { fmt::print("calling ExampleNode destructor.\n"); }

  static ZsList getInputsDesc() {
    ZsList ret = zs_list_obj_default();
    ret.appendSteal(zs_bytes_obj_cstr("0-th arg"));
    ret.appendSteal(zs_bytes_obj_cstr("1-th arg"));
    ret.appendSteal(zs_bytes_obj_cstr("2-th arg"));
    return ret;
  }
  static ZsList getOutputsDesc() {
    ZsList ret = zs_list_obj_default();
    ret.appendSteal(zs_bytes_obj_cstr("output"));
    ret.appendSteal(zs_bytes_obj_cstr("o1"));
    ret.appendSteal(zs_bytes_obj_cstr("output2"));
    return ret;
  }

  static ZsList getAttribDesc() {
    ZsList ret = zs_list_obj_default();
    ret.appendSteal(zs_bytes_obj_cstr("attrib0"));
    return ret;
  }

  static ZsVar get_ui_desc() {
    ZsDict ret = zs_dict_obj_default();
    return ret;
  }

  zs::ResultType apply() override {
    fmt::print("{} is being executed.\n",
               (const char *)zs::get_type_str<ExampleNode>());
    return zs::Result::Success;
  }
  Tagged tag;
};

#define DEF_EXAMPLE_NODE(Name)                                                 \
  struct Example##Name : zs::NodeConcept, zs::NodeInterface<Example##Name> {   \
    zs::ResultType apply() override {                                          \
      fmt::print("{} is being executed.\n", #Name);                            \
      return zs::Result::Success;                                              \
    }                                                                          \
  };

DEF_EXAMPLE_NODE(0)
DEF_EXAMPLE_NODE(1)
DEF_EXAMPLE_NODE(2)
DEF_EXAMPLE_NODE(3)
DEF_EXAMPLE_NODE(4)
DEF_EXAMPLE_NODE(5)
DEF_EXAMPLE_NODE(6)
DEF_EXAMPLE_NODE(7)
DEF_EXAMPLE_NODE(8)
DEF_EXAMPLE_NODE(9)
DEF_EXAMPLE_NODE(10)

ZS_REGISTER_PLUGIN_NODES(ExampleNode, Example0, Example1, Example2, Example3,
                         Example4, Example5, Example6, Example7, Example8,
                         Example9, Example10);