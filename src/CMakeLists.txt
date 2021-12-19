target_sources(${TARGET_NAME_LIB} PRIVATE 
serialization.hxx
)


add_subdirectory(server)
add_subdirectory(logic)
