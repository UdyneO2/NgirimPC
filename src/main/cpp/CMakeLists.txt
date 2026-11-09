cmake_minimum_required(VERSION 3.10)
project(ngirim_pc_native)

# Membangun Shared Library (.dll, .so, .dylib)
add_library(native-lib SHARED native-lib.cpp)

# Mencari JNI bawaan sistem (JDK harus terinstall)
find_package(JNI REQUIRED)

include_directories(${JNI_INCLUDE_DIRS})
target_link_libraries(native-lib ${JNI_LIBRARIES})

# Optimasi tambahan untuk Windows
if(WIN32)
    target_compile_definitions(native-lib PRIVATE -DWIN32_LEAN_AND_MEAN)
endif()