from conan import ConanFile
from conan.tools.cmake import CMakeToolchain

class Project(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators =  "CMakeDeps"

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False #workaround because this leads to useless options in cmake-tools configure
        tc.generate()

    def configure(self):
        self.options["catch2"].with_benchmark = True
        self.options["boost"].header_only = True

    def requirements(self):
        self.requires("catch2/2.13.7")
        self.requires("magic_enum/[>=0.9.5 <10]")
        self.requires("boost/[<2]")
        self.requires("confu_json/[>=1.0.1 <2]")
        self.requires("sml/1.1.5")
        self.requires("durak_computer_controlled_opponent/2.1.0")
        self.requires("confu_soci/[<1]")
        self.requires("corrade/2020.06")
        self.requires("modern_durak_game_shared/latest")
        self.requires("my_web_socket/0.0.7")
