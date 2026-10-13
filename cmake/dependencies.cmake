# dependencies.cmake - Manage project dependencies

# Auto-detect chen-sdk directory by pattern "chen-sdk-*"
file(GLOB CHEN_SDK_DIRS "${PROJECT_SOURCE_DIR}/chen-sdk-*")
if (CHEN_SDK_DIRS)
    list(GET CHEN_SDK_DIRS 0 CHEN_SDK_DIR)
    message(STATUS "Using CHEN_SDK_DIR=${CHEN_SDK_DIR}")
else()
    message(FATAL_ERROR "chen-sdk-* not found under ${PROJECT_SOURCE_DIR}. Run ./deps.py sync")
endif()

include_directories(${CHEN_SDK_DIR}/include)

# 三方依赖（由 deps.py sync 下载到 deps/：头文件 + 静态库），用于编译 chen 头文件
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

add_subdirectory(${CHEN_SDK_DIR})

# 修正 SDK 导出的依赖：SDK 生成的 CMakeLists 硬编码了 /usr/local/lib/libyaml-cpp.so，
# 该路径在 CI/部署环境并不存在；移除它并改用 deps/ 里的 yaml-cpp。
if (DEFINED CHEN_SDK_DEP_LIBS)
    list(REMOVE_ITEM CHEN_SDK_DEP_LIBS "/usr/local/lib/libyaml-cpp.so" "/usr/local/lib/libyaml-cpp.a")
    if (NOT CHEN_SDK_DEP_LIBS MATCHES "yaml-cpp")
        find_library(YAML_CPP_LIB yaml-cpp PATHS ${THIRDPARTY_DEPS_DIR}/lib NO_DEFAULT_PATH)
        if (YAML_CPP_LIB)
            list(APPEND CHEN_SDK_DEP_LIBS ${YAML_CPP_LIB})
            message(STATUS "Using YAML_CPP_LIB=${YAML_CPP_LIB}")
        else()
            message(WARNING "未找到 yaml-cpp（${THIRDPARTY_DEPS_DIR}/lib）")
        endif()
    endif()
endif()
