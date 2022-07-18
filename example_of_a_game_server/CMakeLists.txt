target_sources(${TARGET_NAME_LIB} PRIVATE
    serialization.hxx
    util.cxx
    database.cxx
)

add_subdirectory(game)
add_subdirectory(server)