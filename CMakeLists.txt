cmake_minimum_required(VERSION 3.16)
project(spore_lantern LANGUAGES C CXX)

# The engine sets its own standard; that setting does not carry over to us.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Engine: glgame2d, fetched as an external dependency -------------------
# The upstream repository has no tags, so we track main. The exact commit
# that was fetched is printed below and recorded in README.md.
set(GLGAME2D_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLGAME2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    glgame2d
    GIT_REPOSITORY https://github.com/VarunVF/glgame2d.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(glgame2d)

find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${glgame2d_SOURCE_DIR}
        OUTPUT_VARIABLE GLGAME2D_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    message(STATUS "glgame2d commit: ${GLGAME2D_COMMIT}")
endif()

# --- Game ------------------------------------------------------------------
add_executable(spore-lantern
    src/main.cpp
    src/Game.cpp
    src/Gfx.cpp
    src/Level.cpp
    src/Save.cpp
)
target_link_libraries(spore-lantern PRIVATE glgame2d)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spore-lantern PRIVATE -Wall -Wextra)
endif()

# The game looks for assets/ next to the executable.
add_custom_command(TARGET spore-lantern POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:spore-lantern>/assets
    COMMENT "Copying assets next to the executable"
)
