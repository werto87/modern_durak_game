FROM conanio/gcc13-ubuntu16.04 as CONAN

COPY cmake /home/conan/example_of_a_game_server/cmake
COPY example_of_a_game_server /home/conan/example_of_a_game_server/example_of_a_game_server
COPY test /home/conan/example_of_a_game_server/test
COPY CMakeLists.txt /home/conan/example_of_a_game_server
COPY conanfile.py /home/conan/example_of_a_game_server
COPY main.cxx /home/conan/example_of_a_game_server
COPY ProjectOptions.cmake /home/conan/example_of_a_game_server

WORKDIR /home/conan/example_of_a_game_server
#TODO this should be release not debug but there is a linker error "https://stackoverflow.com/questions/77959920/linker-error-defined-in-discarded-section-with-boost-asio-awaitable-operators
RUN sudo chown -R conan /home/conan  && conan remote add artifactory http://195.128.100.39:8081/artifactory/api/conan/conan-local && conan install . --output-folder=build --settings build_type=Debug  --settings compiler.cppstd=gnu20 --build=missing

WORKDIR /home/conan/example_of_a_game_server/build

#TODO this should be release not debug but there is a linker error "https://stackoverflow.com/questions/77959920/linker-error-defined-in-discarded-section-with-boost-asio-awaitable-operators"
RUN cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DBUILD_TESTS=True -D CMAKE_BUILD_TYPE=Debug

RUN cmake --build .

FROM archlinux:latest

COPY --from=CONAN /home/conan/example_of_a_game_server/build/run_server /home/conan/matchmacking_proxy/example_of_a_game_server

CMD [ "/home/conan/matchmacking_proxy/example_of_a_game_server"]
