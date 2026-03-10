cmake_minimum_required(VERSION 3.16)
project(Geweinet VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# 查找依赖
find_package(Threads REQUIRED)

# readline 库 (用于交互式命令行编辑)
find_package(PkgConfig REQUIRED)
pkg_check_modules(READLINE readline)
if(READLINE_FOUND)
    message(STATUS "Found readline: ${READLINE_LIBRARIES}")
else()
    message(WARNING "readline not found, interactive mode will have limited editing support")
endif()

# nlohmann_json (Header-only, 通过 FetchContent)
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/config.cpp
    src/types.cpp
    src/agent_manager.cpp
    src/router.cpp
    src/iflow_client.cpp
    src/task_scheduler.cpp
    src/ipc.cpp
)

# 可执行文件
add_executable(geweinet ${SOURCES})

target_link_libraries(geweinet
    Threads::Threads
    nlohmann_json::nlohmann_json
)

# 添加 readline 链接
if(READLINE_FOUND)
    target_include_directories(geweinet PRIVATE ${READLINE_INCLUDE_DIRS})
    target_link_libraries(geweinet ${READLINE_LIBRARIES})
    target_compile_definitions(geweinet PRIVATE HAVE_READLINE=1)
endif()

# 测试 (可选)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# 安装
install(TARGETS geweinet DESTINATION bin)
install(DIRECTORY include/geweinet DESTINATION include)
