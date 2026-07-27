cmake_minimum_required(VERSION 3.14)
project(ChessUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Derive the module name from metadata.json — single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

# Universal UI module: you write the .rep + the *Backend class. REP_FILE runs
# repc; the generated *Plugin glue in generated_code/ is compiled automatically.
logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/chess_ui.rep
    SOURCES
        src/chess_ui_backend.h
        src/chess_ui_backend.cpp
    INCLUDE_DIRS
        src
)

# Bake the build-time Stockfish location into the plugin as a fallback engine
# path. Under Nix (stockfish is in nix.packages.build) this embeds the store
# path, which also pulls the engine into the module's runtime closure.
find_program(STOCKFISH_EXECUTABLE stockfish)
if(STOCKFISH_EXECUTABLE)
    target_compile_definitions(${MODULE_NAME}_module_plugin PRIVATE
        LOGOS_CHESS_STOCKFISH_BUILD_PATH="${STOCKFISH_EXECUTABLE}")
endif()
