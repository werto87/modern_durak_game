FROM archlinux:base-devel


COPY . /example_of_a_game_server

RUN pacman-key --init

RUN pacman -Syu --noconfirm

# use pip here because its recomended way to install conan from the conan team
RUN pacman -S cmake git python-pip clang libc++ --noconfirm

RUN pip install conan

RUN conan profile new default --detect

WORKDIR /example_of_a_game_server

RUN conan profile new clang

RUN echo -e "[settings]\nos=Linux\narch=x86_64\ncompiler=clang\ncompiler.libcxx=libc++\n[env]\nCC=/usr/bin/clang\nCXX=/usr/bin/clang++" > /root/.conan/profiles/clang

RUN which clang

RUN conan remote add gitlab https://gitlab.com/api/v4/projects/27217743/packages/conan
# check build parameter there is no libc++ set

RUN rm -rf build && mkdir build && cd build && conan install ..  --profile=clang -s compiler.version=$(clang --version | tr '\n' ' ' | cut -d' ' -f3 | cut -d'.' -f1) --build missing -s build_type=Release  && cmake ..  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_FLAGS=-stdlib=libc++ -DCMAKE_EXE_LINKER_FLAGS="-std=c++20 -stdlib=libc++ -lc++abi" -DCMAKE_BUILD_TYPE=Release  && cmake --build . ; cd ..

FROM archlinux:base

# RUN pacman -S libc++ --noconfirm

COPY --from=0 /example_of_a_game_server/build/bin/project /example_of_a_game_server/project
COPY --from=0 /usr/lib/libc++.a /usr/lib/libc++.a
COPY --from=0 /usr/lib/libc++.so /usr/lib/libc++.so
COPY --from=0 /usr/lib/libc++.so.1 /usr/lib/libc++.so.1
COPY --from=0 /usr/lib/libc++.so.1.0 /usr/lib/libc++.so.1.0
COPY --from=0 /usr/lib/libc++abi.a      /usr/lib/libc++abi.a
COPY --from=0 /usr/lib/libc++abi.so     /usr/lib/libc++abi.so
COPY --from=0 /usr/lib/libc++abi.so.1   /usr/lib/libc++abi.so.1
COPY --from=0 /usr/lib/libc++abi.so.1.0 /usr/lib/libc++abi.so.1.0



CMD [ "/example_of_a_game_server/project"]
