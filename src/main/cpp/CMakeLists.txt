cmake_minimum_required(VERSION 3.14)
project(ngirim_pc_native)

find_package(JNI REQUIRED)

include(FetchContent)

# Mengunduh GLFW (Window Manager PC)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.3.8)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Mengunduh GLAD (OpenGL Loader)
FetchContent_Declare(glad GIT_REPOSITORY https://github.com/libigl/libigl-glad.git GIT_TAG master)
FetchContent_MakeAvailable(glad)

# Mengunduh Dear ImGui
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.89.9)
FetchContent_MakeAvailable(imgui)

add_library(native-lib SHARED 
    native-lib.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(native-lib PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${JNI_INCLUDE_DIRS}
)

target_link_libraries(native-lib PRIVATE glfw glad ${JNI_LIBRARIES})
if(WIN32)
    target_link_libraries(native-lib PRIVATE opengl32)
    target_compile_definitions(native-lib PRIVATE -DWIN32_LEAN_AND_MEAN)
endif()