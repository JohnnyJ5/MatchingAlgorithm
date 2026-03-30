cmake_minimum_required(VERSION 3.16)
project(DatingApp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -O2)

# ── Crow (fetched at configure time, replaces third_party/crow_all.h) ─────────
include(FetchContent)
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.2.1
)
FetchContent_MakeAvailable(Crow)

add_subdirectory(src)
add_subdirectory(server server_build)

# ── Main driver ───────────────────────────────────────────────────────────────
add_executable(main main.cpp)
target_link_libraries(main PRIVATE algorithms)
