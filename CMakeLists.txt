cmake_minimum_required(VERSION 3.15.7)
set(CMAKE_CXX_STANDARD 23)

project(
        modern_durak_game
        VERSION 0.0.1
        LANGUAGES CXX C)
include(cmake/PreventInSourceBuilds.cmake)
include(ProjectOptions.cmake)
myproject_setup_options()
myproject_local_options()
target_compile_features(myproject_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
add_library(myproject::myproject_options ALIAS myproject_options)
add_library(myproject::myproject_warnings ALIAS myproject_warnings)
add_subdirectory(modern_durak_game)
add_executable(create_combination_database create_combination_database.cxx)
target_link_libraries(create_combination_database modern_durak_game)
target_compile_definitions(create_combination_database PRIVATE PATH_TO_DATABASE_TEST="${CMAKE_SOURCE_DIR}/test/database/combination.db")
target_compile_options(create_combination_database PRIVATE)
OPTION(BUILD_TESTS "enable tests" OFF)
IF (BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
ENDIF (BUILD_TESTS)
add_executable(run_server main.cxx)
target_link_libraries(run_server modern_durak_game)
target_compile_definitions(run_server PRIVATE
        # BOOST_ASIO_DISABLE_THREADS
        # LOG_CO_SPAWN_PRINT_EXCEPTIONS
        # LOG_COMPUTER_CONTROLLED_OPPONENT_MASSAGE_RECEIVED
        # LOG_FOR_STATE_MACHINE
        # BOOST_ASIO_ENABLE_HANDLER_TRACKING
        CURRENT_BINARY_DIR="${CMAKE_BINARY_DIR}"
        )
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_custom_target(copy_compile_commands_json ALL
                  DEPENDS ${CMAKE_SOURCE_DIR}/compile_commands.json)

add_custom_command(
  DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
  OUTPUT ${CMAKE_SOURCE_DIR}/compile_commands.json
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/compile_commands.json
          ${CMAKE_SOURCE_DIR}/compile_commands.json
  COMMENT
    "copy compile_commands.json build to compile_commands.json project root so tools can find it more easily"
)
include(cmake/ClangBuildAnalyzer.cmake)
IF (myproject_ENABLE_CLANG_BUILD_ANALYZER)
        myproject_enable_clang_build_analyzer(run_server)
ENDIF ()


