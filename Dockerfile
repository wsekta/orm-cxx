FROM ubuntu:23.10

RUN apt update -y
RUN apt install -y cmake ninja-build g++-13 libstdc++-13-dev

COPY include/ orm-cxx/include/
COPY src/ orm-cxx/src/
COPY externals/ orm-cxx/externals/
COPY CMakeLists.txt orm-cxx/CMakeLists.txt
COPY scripts/ orm-cxx/scripts/

RUN chmod 777 ./orm-cxx/scripts/build_linux_gxx.sh

ENTRYPOINT cd orm-cxx && ./scripts/build_linux_gxx.sh