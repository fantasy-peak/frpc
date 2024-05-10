set_project("frpc")
set_version("1.0.0", {build = "%Y%m%d%H%M"})
set_xmakever("2.8.9")

add_defines("VERSION=\"1.0.0\"")

add_repositories("my_private_repo https://github.com/fantasy-peak/xmake-repo.git")

add_requires("yaml-cpp", "nlohmann_json", "spdlog", "inja", "boost")

set_languages("c++23")
set_policy("check.auto_ignore_flags", false)
add_cxflags("-O2 -Wall -Wextra -pedantic-errors -Wno-missing-field-initializers -Wno-ignored-qualifiers")
add_includedirs("include")

target("frpc")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("yaml-cpp", "nlohmann_json", "spdlog", "inja", "boost")
target_end()
