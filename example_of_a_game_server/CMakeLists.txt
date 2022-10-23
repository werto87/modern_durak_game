target_sources(${TARGET_NAME_LIB} PRIVATE
        serialization.hxx
        util.cxx
        )

add_subdirectory(game)
add_subdirectory(server)