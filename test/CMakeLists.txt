add_executable(_test
        game.cxx
        test.cxx
        )
find_package(Catch2)
target_link_libraries(_test
        myproject_options
        myproject_warnings
        example_of_a_game_server
        Catch2::Catch2WithMain
        )
target_compile_definitions(_test PUBLIC
        # BOOST_ASIO_DISABLE_THREADS
        # _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
        # LOG_CO_SPAWN_PRINT_EXCEPTIONS
        # LOG_COMPUTER_CONTROLLED_OPPONENT_MASSAGE_RECEIVED
        # LOG_FOR_STATE_MACHINE
        # LOG_MY_WEBSOCKET
        # BOOST_ASIO_ENABLE_HANDLER_TRACKING
        CURRENT_BINARY_DIR="${CMAKE_BINARY_DIR}/test"
        )

add_custom_command(
        TARGET _test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/database/combination.db
        ${CMAKE_CURRENT_BINARY_DIR}/test_database/combination.db
)

target_include_directories(_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
add_test(NAME _test
        COMMAND _test -r junit)

