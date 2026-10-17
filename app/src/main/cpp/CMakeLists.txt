cmake_minimum_required(VERSION 3.22.1)
project(tqc CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenXR REQUIRED CONFIG)

set(GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
add_library(native_app_glue STATIC ${GLUE_DIR}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${GLUE_DIR})

set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party)
add_library(tqc SHARED app.cpp gfx.cpp input.cpp models.cpp scene.cpp ui.cpp)
target_include_directories(tqc PRIVATE ${THIRD_PARTY}/cgltf ${THIRD_PARTY}/stb)
target_compile_options(tqc PRIVATE -Wall -Wno-unused-function -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
target_link_libraries(tqc PRIVATE native_app_glue OpenXR::openxr_loader android log EGL GLESv3)
# keep the glue's entry point that NativeActivity looks up
target_link_options(tqc PRIVATE -u ANativeActivity_onCreate)
