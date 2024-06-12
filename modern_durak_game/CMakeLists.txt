##
## Copyright (c) 2024 Waldemar Schneider (w-schneider1987 at web dot de)
##
## Distributed under the Boost Software License, Version 1.0.
## (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)


add_library(modern_durak_game
        )


add_library(myproject::modern_durak_game ALIAS modern_durak_game)


find_package(sml)
find_package(confu_json)
find_package(Boost)
find_package(durak_computer_controlled_opponent)
find_package(confu_soci)
find_package(magic_enum)
find_package(Corrade)
find_package(durak)
find_package(modern_durak_game_shared)
find_package(modern_durak_game_option)


target_link_libraries(modern_durak_game PUBLIC
        sml::sml
        durak_computer_controlled_opponent::durak_computer_controlled_opponent
        modern_durak_game_shared::modern_durak_game_shared
        confu_json::confu_json
        confu_soci::confu_soci
        magic_enum::magic_enum
        boost::boost Corrade::Corrade
        durak::durak
        modern_durak_game_option::modern_durak_game_option
        )


target_compile_features(modern_durak_game PUBLIC cxx_std_20)

add_subdirectory(game)
add_subdirectory(server)
add_subdirectory(util)


target_include_directories(modern_durak_game
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
        )
