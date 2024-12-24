#include "interface/world/NodeInterface.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/zpc_tpls/fmt/core.h"
#include <chrono>
#include <thread>

struct LongRuntimeNode : zs::NodeConcept, zs::NodeInterface<LongRuntimeNode> {
  LongRuntimeNode() { fmt::print("calling LongRuntimeNode constructor.\n"); }
  ~LongRuntimeNode() override {
    fmt::print("calling LongRuntimeNode destructor.\n");
  }

  zs::ResultType apply() override {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    fmt::print("LongRuntimeNode just finished.\n");
    return zs::Result::Success;
  }
};

ZS_REGISTER_PLUGIN_NODE(LongRuntimeNode)