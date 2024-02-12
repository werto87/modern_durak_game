FROM ghcr.io/werto87/arch_linux_docker_image/archlinux_base_devel_conan:2024_02_11_12_32_48 as BUILD

COPY cmake /home/build_user/example_of_a_game_server/cmake
COPY example_of_a_game_server /home/build_user/example_of_a_game_server/example_of_a_game_server
COPY test /home/build_user/example_of_a_game_server/test
COPY CMakeLists.txt /home/build_user/example_of_a_game_server
COPY conanfile.py /home/build_user/example_of_a_game_server
COPY create_combination_database.cxx /home/build_user/example_of_a_game_server
COPY main.cxx /home/build_user/example_of_a_game_server
COPY ProjectOptions.cmake /home/build_user/example_of_a_game_server

WORKDIR /home/build_user/example_of_a_game_server

RUN sudo chown -R build_user /home/build_user  && conan remote add artifactory http://195.128.100.39:8081/artifactory/api/conan/conan-local && conan profile detect && conan install . --output-folder=build --settings compiler.cppstd=gnu20 --build=missing

WORKDIR /home/build_user/example_of_a_game_server/build

RUN cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DBUILD_TESTS=True -D CMAKE_BUILD_TYPE=Release

RUN cmake --build .

RUN ./create_combination_database

RUN test/_test -d yes --order lex

FROM archlinux:latest

COPY --from=BUILD /home/build_user/example_of_a_game_server/build/run_server /home/build_user/example_of_a_game_server/example_of_a_game_server

COPY --from=BUILD /home/build_user/example_of_a_game_server/test/database/combination.db /home/build_user/example_of_a_game_server/build/combination.db

CMD [ "/home/build_user/example_of_a_game_server/example_of_a_game_server" ]
