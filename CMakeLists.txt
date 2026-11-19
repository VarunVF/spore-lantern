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
# Pinned to a commit so engine API changes on main do not break this game
# until it is updated for them. To move to a newer engine: change
# GLGAME2D_PINNED_COMMIT, rebuild, run tests/run_playtest.sh, and update the
# commit recorded in README.md and FRICTION.md.
set(GLGAME2D_PINNED_COMMIT 669d238d4e42d43c102cc2dd7a6e343faddd7f2e)
set(GLGAME2D_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLGAME2D_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Overrides, e.g. to try an engine branch before it is pinned:
#   -DGLGAME2D_REF=main
#   -DGLGAME2D_GIT_REPOSITORY=/path/to/glgame2d -DGLGAME2D_REF=my-branch
set(GLGAME2D_GIT_REPOSITORY "https://github.com/VarunVF/glgame2d.git" CACHE STRING "Where to fetch glgame2d from")
set(GLGAME2D_REF "" CACHE STRING "glgame2d branch, tag or commit to use instead of the pinned commit")
if(GLGAME2D_REF)
    set(GLGAME2D_FETCH_REF ${GLGAME2D_REF})
    message(STATUS "glgame2d: using ${GLGAME2D_REF} instead of the pinned ${GLGAME2D_PINNED_COMMIT}")
else()
    set(GLGAME2D_FETCH_REF ${GLGAME2D_PINNED_COMMIT})
endif()

include(FetchContent)
FetchContent_Declare(
    glgame2d
    GIT_REPOSITORY ${GLGAME2D_GIT_REPOSITORY}
    GIT_TAG        ${GLGAME2D_FETCH_REF}
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

if(EMSCRIPTEN)
    # Web build: spore-lantern.html + .js + .wasm + .data (the packed assets).
    set_target_properties(spore-lantern PROPERTIES SUFFIX ".html")
    target_link_options(spore-lantern PRIVATE
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=FS,ENV,addRunDependency,removeRunDependency"
        "-lidbfs.js"
    )
    set_property(TARGET spore-lantern APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
    # Repack when an asset changes (the .data file is made at link time).
    file(GLOB_RECURSE SPORE_ASSET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/*)
    set_property(TARGET spore-lantern APPEND PROPERTY LINK_DEPENDS ${SPORE_ASSET_FILES})
    return()
endif()

# The game looks for assets/ next to the executable. This runs on every build
# (not only when the executable relinks) so edited assets are always picked up.
add_custom_target(spore-lantern-assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:spore-lantern>/assets
    COMMENT "Copying assets next to the executable"
)
add_dependencies(spore-lantern-assets spore-lantern)
