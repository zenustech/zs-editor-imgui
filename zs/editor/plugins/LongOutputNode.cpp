#include "interface/world/NodeInterface.hpp"
#include "zensim/ZpcReflection.hpp"
#include "zensim/zpc_tpls/fmt/core.h"

struct LongOutputNode : zs::NodeConcept, zs::NodeInterface<LongOutputNode> {
  LongOutputNode() { printf("calling LongOutputNode constructor.\n"); }
  ~LongOutputNode() override {
    printf("calling LongOutputNode destructor.\n");
  }

  zs::ResultType apply() override {
    for (int i = 0; i < 996; ++i)
      printf("[%d] 1233123;zxlckvjzx;lkjpwoeiuf20p1398jzx;clvkm/"
                 ",.mawsel;foiwjaepfoiwajfe;lkj;xclkzvj;lkj;lkj;aslkdfjweoij\n",
                 i);
    printf("LongOutputNode just finished.\n");
    return zs::Result::Success;
  }
};

ZS_REGISTER_PLUGIN_NODE(LongOutputNode)