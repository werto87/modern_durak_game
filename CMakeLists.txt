cmake_minimum_required(VERSION 3.15.7)
if (NOT DEFINED CMAKE_CXX_STANDARD) # Only set the cxx_standard if it is not set by someone else
    set(CMAKE_CXX_STANDARD 20)
endif ()
project(
        example_of_a_game_server
        VERSION 0.0.1
        LANGUAGES CXX C)
include(cmake/PreventInSourceBuilds.cmake)
include(ProjectOptions.cmake)
myproject_setup_options()
myproject_local_options()
target_compile_features(myproject_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})
add_library(myproject::myproject_options ALIAS myproject_options)
add_library(myproject::myproject_warnings ALIAS myproject_warnings)
add_subdirectory(example_of_a_game_server)
add_executable(create_combination_database create_combination_database.cxx)
target_link_libraries(create_combination_database example_of_a_game_server)
target_compile_definitions(create_combination_database PRIVATE PATH_TO_DATABASE_TEST="${CMAKE_SOURCE_DIR}/test/database/combination.db")
target_compile_options(create_combination_database PRIVATE)
OPTION(BUILD_TESTS "enable tests" OFF)
IF (BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
ENDIF (BUILD_TESTS)
add_executable(run_server main.cxx)
target_link_libraries(run_server example_of_a_game_server)
target_compile_definitions(run_server PRIVATE
        # BOOST_ASIO_DISABLE_THREADS
        # _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
        # LOG_CO_SPAWN_PRINT_EXCEPTIONS
        # LOG_COMPUTER_CONTROLLED_OPPONENT_MASSAGE_RECEIVED
        # LOG_FOR_STATE_MACHINE
        # LOG_MY_WEBSOCKET
        # BOOST_ASIO_ENABLE_HANDLER_TRACKING
        CURRENT_BINARY_DIR="${CMAKE_BINARY_DIR}"
        )