# Host renderer: cmake -S tools/render_host -B out/render_host && cmake --build out/render_host
cmake_minimum_required(VERSION 3.16)
project(render_host CXX)
set(CMAKE_CXX_STANDARD 17)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)
set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party)
set(OPENXR_TAG release-1.1.49)
foreach(h openxr.h openxr_platform_defines.h)
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/include/openxr/${h})
        file(DOWNLOAD https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/${OPENXR_TAG}/include/openxr/${h}
             ${CMAKE_BINARY_DIR}/include/openxr/${h} STATUS st)
    endif()
endforeach()

add_executable(render_host main.cpp xr_stubs.cpp ${APP}/scene.cpp ${APP}/gfx.cpp ${APP}/input.cpp ${APP}/models.cpp
               ${APP}/ui.cpp ${APP}/text.cpp)
target_include_directories(render_host PRIVATE ${APP} ${CMAKE_BINARY_DIR}/include ${THIRD_PARTY}/cgltf
                           ${THIRD_PARTY}/stb)
target_link_libraries(render_host PRIVATE EGL GLESv2)
