#pragma once
#include <string>
#include <vector>

namespace zs {

    std::string python_evaluate(const std::string& script,
        const std::vector<std::string>& args);

}