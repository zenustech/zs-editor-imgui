# 构建 {#ZsEditorImgui_Build}


## 配置依赖

拉取项目代码：
```console
git clone https://github.com/zenustech/zs-editor-imgui.git --recursive
```

绝大多数依赖通过子模块引入一并构建。若clone时未带--recursive选项，则需手动更新子模块：
```console
git submodule update --init --recursive
```

此外还有少量第三方依赖，如[vulkan](https://vulkan.lunarg.com/), boost filesystem/process等，是以[find_package](https://cmake.org/cmake/help/latest/command/find_package.html)形式引用。
推荐开发者通过包管理器（apt、vcpkg、brew/macports等）安装或手动从源码构建使用。

## 构建项目

```console
cmake -Bbuild -DZS_ENABLE_USD=ON -DZS_USD_ROOT=${path_to_your_USD_installation}
cmake --build build --config Release --parallel 12 --target zs_editor_imgui
```

- **注意**：（不推荐）若USD模块未配置好，可设置-DZS_ENABLE_USD=OFF来关闭相关模块。
- **注意**：若想启用precompile header来加速编译，可在cmake configure时加入-DZS_ENABLE_PCH=ON选项。

若开启zpcjit即时编译模块，则在cmake configure时cmake -Bbuild后附加上-DLLVM_DIR=${path_to_your_llvm_cmake_config_file}，例如/usr/local/lib/cmake/llvm。

若需启用openvdb模块，则附加上-DCMAKE_MODULE_PATH=${path_to_your_openvdb_find_file}，例如/.../openvdb/cmake；同时指定使用2020.3版本tbb，-DTBB_DIR=${path_to_tbb2020.3_config_file}，例如/.../tbb2020.3/tbb/cmake。
