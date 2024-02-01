from conan import ConanFile


class Project(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"

    def configure(self):
        self.options["catch2"].with_main = True
        self.options["catch2"].with_benchmark = True
        self.options["boost"].header_only = True

    def requirements(self):
        self.requires("catch2/2.13.7")
        self.requires("magic_enum/[>=0.9.5 <10]")
        self.requires("boost/1.84.0")
        self.requires("confu_json/1.0.1")
        self.requires("range-v3/0.12.0")
        self.requires("sml/1.1.5")
        self.requires("durak_computer_controlled_opponent/0.0.16")
        self.requires("confu_soci/0.3.15")
        self.requires("corrade/2020.06")
        self.requires("pipes/1.0.0")
        self.requires("durak/1.0.0")


