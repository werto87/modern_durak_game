FROM ghcr.io/werto87/arch_linux_docker_image/archlinux_base_devel_conan:2024_06_12_09_54_36 AS build

COPY cmake /home/build_user/modern_durak_game/cmake
COPY modern_durak_game /home/build_user/modern_durak_game/modern_durak_game
COPY test /home/build_user/modern_durak_game/test
COPY CMakeLists.txt /home/build_user/modern_durak_game
COPY conanfile.py /home/build_user/modern_durak_game
COPY create_combination_database.cxx /home/build_user/modern_durak_game
COPY main.cxx /home/build_user/modern_durak_game
COPY ProjectOptions.cmake /home/build_user/modern_durak_game

WORKDIR /home/build_user/modern_durak_game

RUN sudo chown -R build_user /home/build_user && conan remote add modern_durak http://modern-durak.com:8081/artifactory/api/conan/conan-local && conan profile detect && conan remote login modern_durak read -p 'B2"bi%y@SQhqP~X' && conan install . --output-folder=build --settings compiler.cppstd=gnu23 --build=missing

WORKDIR /home/build_user/modern_durak_game/build

#workaround false positive with gcc release  myproject_WARNINGS_AS_ERRORS=Off 
RUN cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DBUILD_TESTS=True -D CMAKE_BUILD_TYPE=Release -D  myproject_WARNINGS_AS_ERRORS=Off 

RUN cmake --build .

RUN ./create_combination_database

RUN test/_test -d yes --order lex

FROM ghcr.io/werto87/arch_linux_docker_image/archlinux_base:2024_06_13_07_30_51 

COPY --from=build /home/build_user/modern_durak_game/build/run_server /home/build_user/modern_durak_game/modern_durak_game

COPY --from=build /home/build_user/modern_durak_game/test/database/combination.db /home/build_user/modern_durak_game/build/combination.db

CMD [ "/home/build_user/modern_durak_game/modern_durak_game" ]
