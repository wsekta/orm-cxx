How to contribute
=================


Getting Started
---------------

- Pick an issue from [Issues](https://github.com/wsekta/orm-cxx/issues).
- Fork the repository on GitHub
- **Working on your first Pull Request?** You can learn how from this *free* series [How to Contribute to an Open Source Project on GitHub](https://kcd.im/pull-request) 

Making Changes
--------------

- Create a feature/bug branch from main branch:

  ``git checkout -b feature/feature-name``

  Please avoid working directly on the ``main`` branch.
- Make commits of logical units.
- Make sure you have added the necessary tests for your changes.
- Run *all* the tests to assure nothing else was accidentally broken.
- If you've added a new file to your project with non-Latin characters, ensure that the file encoding is set to <strong>Unicode (UTF-8 without signature) - Codepage 65001</strong> in Microsoft Visual Studio Code.

Submitting Changes
------------------

- Format code with ``clang-format src/**/*.cpp src/**/*.h include/**/*.h -i -style=file``
- Push your changes to the branch in your fork of the repository.
- Submit a pull request to the repository.

Local verification
------------------

On Windows, run the project checks from Ubuntu on WSL with the Clang preset:

```bash
cd /mnt/c/Users/Gosia/orm-cxx
cmake --preset clang-libc++ -DCODE_COVERAGE=ON
cmake --build --preset clang-libc++
ctest --test-dir build --output-on-failure
```

If the local LLVM tools are installed with versioned names, configure the coverage build with explicit paths:

```bash
cmake -S . -B /tmp/orm-cxx-build-coverage -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_CXX_FLAGS='-stdlib=libc++' \
  -DCMAKE_EXE_LINKER_FLAGS='-stdlib=libc++ -lc++abi' \
  -DCMAKE_INCLUDE_PATH=/usr/lib/llvm-18/include/c++/v1 \
  -DCMAKE_LIBRARY_PATH=/usr/lib/llvm-18/lib \
  -DLLVM_COV_PATH=/usr/bin/llvm-cov-18 \
  -DLLVM_PROFDATA_PATH=/usr/bin/llvm-profdata-18 \
  -DCODE_COVERAGE=ON
cmake --build /tmp/orm-cxx-build-coverage
ctest --test-dir /tmp/orm-cxx-build-coverage --output-on-failure
cmake --build /tmp/orm-cxx-build-coverage --target orm-cxx-ccov-all-report
cmake --build /tmp/orm-cxx-build-coverage --target orm-cxx-ccov-all-export
```

Keep documentation changes in the same pull request as user-facing behavior changes.
