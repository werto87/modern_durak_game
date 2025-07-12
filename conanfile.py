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
        self.options["my_web_socket"].log_co_spawn_print_exception = True
        self.options["my_web_socket"].log_write = True
        self.options["my_web_socket"].log_read = True

    def requirements(self):
        self.requires("durak/1.2.0",force=True)
        self.requires("catch2/2.13.7")
        self.requires("magic_enum/0.9.6")
        self.requires("boost/1.86.0",force=True)
        self.requires("confu_json/[>=1.1.1 <2]@modern-durak",force=True)
        self.requires("sml/1.1.8") #DO NOT CHANGE THIS. starting with version 1.1.9 process_event returns ins some cases false where before it returned true
        self.requires("durak_computer_controlled_opponent/2.2.2")
        self.requires("confu_soci/[<1]")
        self.requires("corrade/2025.06")
        self.requires("modern_durak_game_shared/latest")
        self.requires("my_web_socket/0.1.3")
