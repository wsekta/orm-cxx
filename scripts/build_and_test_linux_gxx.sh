mkdir build-linux-gxx

chmod 777 build-linux-gxx

cd build-linux-gxx || exit 1

cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
         -GNinja || exit 1

cmake --build . --config Debug || exit 1

ctest -C Debug --output-on-failure