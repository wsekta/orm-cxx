FROM ubuntu:23.10

RUN apt -y update
RUN apt -y upgrade
RUN apt install -y cmake ninja-build g++-13 libstdc++-13-dev sqlite3 libsqlite3-dev

COPY include/ orm-cxx/include/
COPY src/ orm-cxx/src/
COPY tests/ orm-cxx/tests/
COPY externals/ orm-cxx/externals/
COPY CMakeLists.txt orm-cxx/CMakeLists.txt
COPY scripts/ orm-cxx/scripts/

ENV SOCI_BACKENDS_PATH=/orm-cxx/build-linux-gxx/lib

RUN chmod 777 ./orm-cxx/scripts/build_and_test_linux_gxx.sh

ENTRYPOINT cd orm-cxx && ./scripts/build_and_test_linux_gxx.sh