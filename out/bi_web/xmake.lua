set_project("web")
set_version("0.0.1", {build = "%Y%m%d%H%M"})
set_xmakever("2.8.5")

add_repositories("my_private_repo https://github.com/fantasy-peak/xmake-repo.git")

add_requires("nlohmann_json", "spdlog", "drogon", "asio", "cppzmq")

set_languages("c++23")
set_policy("check.auto_ignore_flags", false)
add_cxflags("-O2 -Wall -Wextra -pedantic-errors -Wno-missing-field-initializers -Wno-ignored-qualifiers")
add_includedirs("include")

target("bi-web")
    set_kind("binary")
    add_files("src/main.cpp")
    add_packages("drogon", "nlohmann_json", "spdlog", "asio", "cppzmq")
target_end()
