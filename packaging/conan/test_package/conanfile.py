import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout


class OrmCxxTestConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "VirtualRunEnv"
    test_type = "explicit"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        options = self.dependencies["orm-cxx"].options
        tc = CMakeToolchain(self)
        tc.variables["ORM_CXX_EXPECT_SQLITE"] = bool(options.with_sqlite3)
        tc.variables["ORM_CXX_EXPECT_POSTGRESQL"] = bool(options.with_postgresql)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            for executable in ("test_package", "test_reflection"):
                self.run(
                    os.path.join(self.cpp.build.bindirs[0], executable),
                    env="conanrun",
                )
