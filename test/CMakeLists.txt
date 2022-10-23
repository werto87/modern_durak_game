set(TEST_TARGET_NAME _test)
add_executable(${TEST_TARGET_NAME}
        test.cxx
        game.cxx
        )

target_link_libraries(${TEST_TARGET_NAME}
        ${TARGET_NAME_LIB}
        CONAN_PKG::catch2

        # pthread
        )
target_include_directories(${TEST_TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        )

target_compile_definitions(${TEST_TARGET_NAME} PUBLIC
        CURRENT_BINARY_DIR=\"${CMAKE_CURRENT_BINARY_DIR}\"
        )

add_test(NAME ${TEST_TARGET_NAME}
        COMMAND ${TEST_TARGET_NAME} -r junit
        )
