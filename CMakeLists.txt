cmake_minimum_required(VERSION 3.18)

set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)

include("project_info.in")

include(CheckLanguage)
check_language(CUDA)
if (CMAKE_CUDA_COMPILER)
	enable_language(CUDA)
endif()

project(zs-editor-imgui
	VERSION ${project_version}
	DESCRIPTION ${project_description}
	LANGUAGES C CXX)

option(ZS_EDITOR_IMGUI_ENABLE_DOC "Build Doc" OFF)
option(ZS_ENABLE_USD "Build USD module" ON)

if (CMAKE_VERSION VERSION_LESS "3.21")
    # ref: VulkanMemoryAllocator
    # https://cmake.org/cmake/help/latest/variable/PROJECT_IS_TOP_LEVEL.html
    string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} PROJECT_IS_TOP_LEVEL)
endif()
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_path(GET CMAKE_CXX_COMPILER STEM LAST_ONLY ZPC_CXX_COMPILER_BINARY)
message(STATUS "Current C++ compiler is ${ZPC_CXX_COMPILER_BINARY} (${CMAKE_CXX_COMPILER})")
if(${ZPC_CXX_COMPILER_BINARY} STREQUAL "icx")
    add_compile_options(-Wno-error=register -Wno-enum-constexpr-conversion -Wno-implicit-function-declaration)
endif()

# 
# set(ZS_ENABLE_VULKAN_VALIDATION OFF)

add_subdirectory(deps/world EXCLUDE_FROM_ALL)

if (ZS_EDITOR_IMGUI_ENABLE_DOC)
    # add_subdirectory(doc)
endif()

###########
## deps  ##
###########

# glfw
set(GLFW_LIBRARY_TYPE STATIC)
add_subdirectory(deps/glfw EXCLUDE_FROM_ALL)

# imgui specific
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/deps/imgui)
add_library(imgui_core STATIC 
	${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
	${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
	# ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_demo.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui_core PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp ..)
##############################################
target_compile_definitions(imgui_core PUBLIC -DImTextureID=ImU64)
target_compile_definitions(imgui_core PUBLIC -DIMGUI_DEFINE_MATH_OPERATORS)
target_compile_definitions(imgui_core PUBLIC -DIMGUI_USE_WCHAR32)
##############################################
target_link_libraries(imgui_core PUBLIC glfw)
target_include_directories(imgui_core PUBLIC "$ENV{VULKAN_SDK}/include")
target_compile_features(imgui_core PUBLIC cxx_std_17)

# zs_imgui_node_editor
set(IMGUI_EDITOR_DIR ${PROJECT_SOURCE_DIR}/deps/zs_imgui_node_editor)
add_library(imgui_editor_core STATIC 
	${IMGUI_EDITOR_DIR}/crude_json.cpp
	${IMGUI_EDITOR_DIR}/imgui_canvas.cpp
	${IMGUI_EDITOR_DIR}/imgui_node_editor_api.cpp
	${IMGUI_EDITOR_DIR}/imgui_node_editor.cpp
)
target_include_directories(imgui_editor_core PUBLIC ${IMGUI_EDITOR_DIR})
target_link_libraries(imgui_editor_core PUBLIC imgui_core)
set(IMGUI_NODE_EDITOR_ROOT_DIR ${IMGUI_EDITOR_DIR})

# zs_guizmo
set(IMGUI_GUIZMO_DIR ${PROJECT_SOURCE_DIR}/deps/zs_imguizmo)
add_library(imgui_guizmo_core STATIC 
	${IMGUI_GUIZMO_DIR}/ImCurveEdit.cpp
	${IMGUI_GUIZMO_DIR}/ImGradient.cpp
	${IMGUI_GUIZMO_DIR}/ImGuizmo.cpp
	${IMGUI_GUIZMO_DIR}/ImSequencer.cpp
)
target_include_directories(imgui_guizmo_core PUBLIC ${IMGUI_GUIZMO_DIR})
target_link_libraries(imgui_guizmo_core PUBLIC imgui_core)
set(IMGUI_GUIZMO_ROOT_DIR ${IMGUI_GUIZMO_DIR})

# imgui file dialog
add_subdirectory(deps/imgui_file_dialog)
target_link_libraries(ImGuiFileDialog PUBLIC imgui_core)

# imfont
add_library(imfont INTERFACE)
target_include_directories(imfont INTERFACE deps/imgui_fonts)

############
## editor ##
############

add_executable(zs_editor_imgui 
	zs/editor/GuiWindow.cpp
	zs/editor/GuiWindowCallbacks.cpp
	zs/editor/GuiWindowMaintenance.cpp
	zs/editor/GuiWindowImgui.cpp

	zs/editor/SceneEditor.cpp
	zs/editor/SceneEditorOIT.cpp
	zs/editor/SceneEditorGUI.cpp
	zs/editor/SceneEditorAugment.cpp
	zs/editor/SceneEditorOutline.cpp
	zs/editor/SceneEditorPicking.cpp
	zs/editor/SceneEditorLighting.cpp
	zs/editor/SceneEditorOcclusionQuery.cpp

	zs/editor/widgets/DetailWidgetComponent.cpp
	zs/editor/widgets/SceneWidgetComponent.cpp
	zs/editor/widgets/SequencerComponent.cpp
	zs/editor/widgets/TreeWidgetComponent.cpp
	zs/editor/widgets/TreeWidgetUsdComponent.cpp
	zs/editor/widgets/TreeWidgetPrimitiveComponent.cpp
	zs/editor/widgets/TextEditorComponent.cpp
	zs/editor/widgets/TermWidgetComponent.cpp
	zs/editor/widgets/AssetBrowserComponent.cpp
	zs/editor/widgets/GraphWidgetComponent.cpp
	zs/editor/widgets/GraphWidgetGraph.cpp
	zs/editor/widgets/GraphWidgetNode.cpp
	zs/editor/widgets/GraphWidgetPin.cpp
	zs/editor/widgets/GraphWidgetLink.cpp

	zs/editor/widgets/WidgetEvent.cpp
	zs/editor/widgets/WidgetComponent.cpp
	zs/editor/widgets/ResourceWidgetComponent.cpp

	zs/editor/widgets/utilities/textselect.cpp
	zs/editor/widgets/utilities/drawing.cpp
	zs/editor/widgets/utilities/widgets.cpp

	zs/editor/python/widgets/PyEditorGraph.cpp
	zs/editor/python/PyEval.cpp

	zs/editor/GlfwSystem.cpp
	zs/editor/ImguiSystem.cpp
	zs/editor/ImguiRenderer.cpp

	zs/editor/main.cpp 
	)
target_compile_definitions(zs_editor_imgui PRIVATE -D_WIN32_WINNT=0x0601)
target_link_libraries(zs_editor_imgui PUBLIC
	glfw imgui_core imgui_editor_core imgui_guizmo_core 
	zs_world 
	tinygltf utf8cpp ImGuiFileDialog imfont
)
find_package(Boost COMPONENTS process)
if (TARGET Boost::process)
	target_link_libraries(zs_editor_imgui PRIVATE
		Boost::process
	)
endif()

target_compile_features(zs_editor_imgui PUBLIC cxx_std_20)

target_include_directories(zs_editor_imgui PRIVATE zs)

target_link_libraries(zs_editor_imgui PRIVATE zpc_jit_py)

########################
## additional modules ##
########################

execute_process(
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/resource
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/zs/editor/scripts ${CMAKE_BINARY_DIR}/resource/scripts
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/zs/editor/shaders ${CMAKE_BINARY_DIR}/resource/shaders
)