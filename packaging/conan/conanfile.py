import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy, get, rmdir
from conan.tools.scm import Version


required_conan_version = ">=2.1"


class OrmCxxConan(ConanFile):
    name = "orm-cxx"
    description = "A C++20 object-relational mapper with compile-time model reflection"
    license = "MIT"
    homepage = "https://github.com/wsekta/orm-cxx"
    url = "https://github.com/conan-io/conan-center-index"
    topics = ("orm", "database", "sqlite", "postgresql", "reflection")
    package_type = "static-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "fPIC": [True, False],
        "with_sqlite3": [True, False],
        "with_postgresql": [True, False],
    }
    default_options = {
        "fPIC": True,
        "with_sqlite3": True,
        "with_postgresql": False,
    }

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self, src_folder="src")

    def requirements(self):
        self.requires(
            "soci/4.1.2",
            transitive_headers=True,
            transitive_libs=True,
            options={
                "with_sqlite3": bool(self.options.with_sqlite3),
                "with_postgresql": bool(self.options.with_postgresql),
            },
        )

    def build_requirements(self):
        self.tool_requires("cmake/[>=3.22 <4]")

    def validate(self):
        check_min_cppstd(self, 20)
        minimum_compiler = {"gcc": "13", "clang": "18", "msvc": "193"}.get(str(self.settings.compiler))
        if minimum_compiler and Version(self.settings.compiler.version) < minimum_compiler:
            raise ConanInvalidConfiguration(
                f"orm-cxx requires {self.settings.compiler} {minimum_compiler} or newer"
            )
        # Conan models MSVC 19.38 as version=193, update=8. Older profiles may
        # omit update, so only reject an explicitly known unsupported update.
        if str(self.settings.compiler) == "msvc" and str(self.settings.compiler.version) == "193":
            update = self.settings.compiler.get_safe("update")
            if update is not None and Version(update) < "8":
                raise ConanInvalidConfiguration("orm-cxx requires MSVC 19.38 or newer")
        soci_options = self.dependencies["soci"].options
        for backend in ("with_sqlite3", "with_postgresql"):
            if self.options.get_safe(backend) and not soci_options.get_safe(backend):
                raise ConanInvalidConfiguration(
                    f"orm-cxx:{backend}=True requires soci:{backend}=True"
                )

    def source(self):
        get(self, **self.conan_data["sources"][self.version], strip_root=True)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["ORM_CXX_USE_SYSTEM_SOCI"] = True
        tc.variables["ORM_CXX_BUILD_TESTS"] = False
        tc.variables["ORM_CXX_BUILD_EXAMPLES"] = False
        tc.variables["ORM_CXX_ENABLE_COVERAGE"] = False
        tc.variables["ORM_CXX_WARNINGS_AS_ERRORS"] = False
        tc.variables["ORM_CXX_ENABLE_SQLITE_BACKEND"] = self.options.with_sqlite3
        tc.variables["ORM_CXX_ENABLE_POSTGRESQL_BACKEND"] = self.options.with_postgresql
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        for license_file in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            copy(
                self,
                license_file,
                src=self.source_folder,
                dst=os.path.join(self.package_folder, "licenses"),
            )
        cmake = CMake(self)
        cmake.install()
        # Conan generates relocatable dependency-aware configs for each consumer.
        rmdir(self, os.path.join(self.package_folder, "lib", "cmake"))
        rmdir(self, os.path.join(self.package_folder, "share"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "orm-cxx")
        self.cpp_info.set_property("cmake_target_name", "orm-cxx::orm-cxx")

        reflection = self.cpp_info.components["reflection"]
        reflection.set_property("cmake_target_name", "orm-cxx::reflection")
        reflection.libdirs = []

        core = self.cpp_info.components["core"]
        core.set_property("cmake_target_name", "orm-cxx::core")
        core.libs = ["orm-cxx"]
        core.requires = ["reflection", "soci::soci_core"]
        core.defines = [
            f"ORM_CXX_ENABLE_SQLITE_BACKEND={int(bool(self.options.with_sqlite3))}",
            f"ORM_CXX_ENABLE_POSTGRESQL_BACKEND={int(bool(self.options.with_postgresql))}",
        ]
        if self.options.with_sqlite3:
            core.requires.append("soci::soci_sqlite3")
        if self.options.with_postgresql:
            core.requires.append("soci::soci_postgresql")
