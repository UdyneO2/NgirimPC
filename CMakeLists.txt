cmake_minimum_required(VERSION 3.20)

# Versi diberikan CI dari nama tag: cmake -DNGIRIMPC_VERSION=2.0.0 (tag "v2.0.0" -> "2.0.0")
set(NGIRIMPC_VERSION "0.0.0-dev" CACHE STRING "Versi NgirimPC (semver, tanpa awalan v)")
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)?(\\.[0-9]+)?" NGR_NUMERIC "${NGIRIMPC_VERSION}")
if(NOT NGR_NUMERIC)
  set(NGR_NUMERIC "0.0.0")
endif()
string(REPLACE "." ";" NGR_PARTS "${NGR_NUMERIC}")
list(LENGTH NGR_PARTS NGR_LEN)
while(NGR_LEN LESS 3)
  list(APPEND NGR_PARTS 0)
  math(EXPR NGR_LEN "${NGR_LEN}+1")
endwhile()
list(GET NGR_PARTS 0 NGR_MAJ)
list(GET NGR_PARTS 1 NGR_MIN)
list(GET NGR_PARTS 2 NGR_PAT)
set(NGIRIMPC_VER_COMMA "${NGR_MAJ},${NGR_MIN},${NGR_PAT},0")

project(NgirimPC VERSION ${NGR_MAJ}.${NGR_MIN}.${NGR_PAT} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # CRT statis: satu exe, tanpa VC++ Redist
  add_compile_options(/utf-8 /permissive- /W3)
endif()

configure_file(res/ngirimpc_version.h.in ${CMAKE_BINARY_DIR}/gen/ngirimpc_version.h @ONLY)

# ---------------------------------------------------------------- inti portabel + tes (semua OS)
add_library(ng_core STATIC
  src/core/localops.cpp src/core/mdns.cpp src/core/netio.cpp src/core/proto.cpp
  src/core/updater_core.cpp src/core/updater.cpp src/core/qr.cpp)
target_include_directories(ng_core PUBLIC src/core)
find_package(Threads REQUIRED)
target_link_libraries(ng_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(ng_core PUBLIC ws2_32 iphlpapi)
  target_compile_definitions(ng_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

option(NGIRIMPC_BUILD_TESTS "Bangun tes unit (tidak dipakai di CI)" OFF)
if(NGIRIMPC_BUILD_TESTS)
  enable_testing()
  add_executable(ng_updater_test tests/updater_test.cpp)
  target_link_libraries(ng_updater_test PRIVATE ng_core)
  add_test(NAME updater COMMAND ng_updater_test)
  add_executable(ng_qr_test tests/qr_test.cpp)
  target_link_libraries(ng_qr_test PRIVATE ng_core)
  add_test(NAME qr COMMAND ng_qr_test)
  add_executable(ng_proto_test tests/proto_test.cpp)
  target_link_libraries(ng_proto_test PRIVATE ng_core)
  add_test(NAME protocol COMMAND ng_proto_test)
endif()

# ---------------------------------------------------------------- aplikasi Windows
option(NGIRIMPC_BUILD_APP "Bangun NgirimPC.exe (butuh UI lengkap)" ON)
if(WIN32 AND NGIRIMPC_BUILD_APP)
  include(FetchContent)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(imgui)

  add_executable(NgirimPC WIN32
    src/win/main_win.cpp src/win/plat_win.cpp src/app/app.cpp src/app/i18n.cpp
    res/ngirimpc.rc
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  add_compile_definitions(UNICODE _UNICODE)
  target_include_directories(NgirimPC PRIVATE
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${CMAKE_BINARY_DIR}/gen res src)
  target_compile_definitions(NgirimPC PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:NGIRIMPC_VERSION="${NGIRIMPC_VERSION}">)
  target_link_libraries(NgirimPC PRIVATE ng_core winhttp d3d11 dxgi dwmapi shell32 ole32 uuid user32 gdi32 advapi32)
  # Manifest sudah ditanam lewat res/ngirimpc.rc; tanpa ini linker membuat manifest kedua -> CVT1100 duplicate resource.
  if(MSVC)
    target_link_options(NgirimPC PRIVATE /MANIFEST:NO)
  endif()
  set_target_properties(NgirimPC PROPERTIES OUTPUT_NAME "NgirimPC")
endif()
