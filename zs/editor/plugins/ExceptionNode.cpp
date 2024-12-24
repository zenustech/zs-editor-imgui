#include "interface/world/NodeInterface.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/zpc_tpls/fmt/core.h"
#include <stdexcept>

struct UnhandledExceptionNode : zs::NodeConcept,
                                zs::NodeInterface<UnhandledExceptionNode> {
  UnhandledExceptionNode() {
    fmt::print("calling UnhandledExceptionNode constructor.\n");
  }
  ~UnhandledExceptionNode() override {
    fmt::print("calling UnhandledExceptionNode  destructor.\n");
  }

  zs::ResultType apply() override {
    throw std::runtime_error("damn");
    return zs::Result::Fail;
  }
};

ZS_REGISTER_PLUGIN_NODE(UnhandledExceptionNode)