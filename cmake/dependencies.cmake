# dependencies.cmake - Manage project dependencies

# Auto-detect chen-sdk directory by pattern "chen-sdk-*"
file(GLOB CHEN_SDK_DIRS "${PROJECT_SOURCE_DIR}/chen-sdk-*")
if (CHEN_SDK_DIRS)
    list(GET CHEN_SDK_DIRS 0 CHEN_SDK_DIR)
    message(STATUS "Using CHEN_SDK_DIR=${CHEN_SDK_DIR}")
else()
    message(FATAL_ERROR "chen-sdk-* not found under ${PROJECT_SOURCE_DIR}. Please fetch chen-sdk.")
endif()

include_directories(${CHEN_SDK_DIR}/include)

# 三方依赖（由 deps.py sync / thirdparty 下载到 deps/：头文件 + 静态库）
# 仅用于编译 chen 头文件（protobuf/yaml-cpp/tinyxml2/jsoncpp/hiredis-vip），不静态链接进模块，
# 运行时符号由预编译 libchen.so 提供，避免重复符号。
set(THIRDPARTY_DEPS_DIR ${PROJECT_SOURCE_DIR}/deps)
if (EXISTS ${THIRDPARTY_DEPS_DIR}/include)
    include_directories(${THIRDPARTY_DEPS_DIR}/include)
    list(PREPEND CMAKE_PREFIX_PATH ${THIRDPARTY_DEPS_DIR})
    list(PREPEND CMAKE_LIBRARY_PATH ${THIRDPARTY_DEPS_DIR}/lib)
    list(PREPEND CMAKE_INCLUDE_PATH ${THIRDPARTY_DEPS_DIR}/include)
    set(Protobuf_ROOT ${THIRDPARTY_DEPS_DIR})
    set(Protobuf_USE_STATIC_LIBS ON)
    message(STATUS "Using THIRDPARTY_DEPS_DIR=${THIRDPARTY_DEPS_DIR}")
else()
    message(WARNING "deps/ 未就绪，运行 ./deps.py sync 下载三方依赖（头文件/静态库）")
endif()

set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
add_subdirectory(${CHEN_SDK_DIR})
unset(CMAKE_FIND_PACKAGE_PREFER_CONFIG)
