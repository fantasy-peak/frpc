#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <unordered_map>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>
#include <boost/algorithm/string.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/unordered_map.hpp>
#include <inja/inja.hpp>

#include "cmdline.h"
#include "utils.h"

inline std::unordered_map<std::string, std::string> CPP_TYPE_TABLE{
    {"bool", "bool"},
    {"int8_t", "int8_t"},
    {"uint8_t", "uint8_t"},
    {"int16_t", "int16_t"},
    {"uint16_t", "uint16_t"},
    {"int32_t", "int32_t"},
    {"uint32_t", "uint32_t"},
    {"int64_t", "int64_t"},
    {"uint64_t", "uint64_t"},
    {"float", "float"},
    {"double", "double"},
};

void copy_directory(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!std::filesystem::exists(destination)) {
        std::filesystem::create_directories(destination);
    }
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        const auto destination_entry = destination / entry.path().filename();
        if (std::filesystem::is_directory(entry.status())) {
            copy_directory(entry, destination_entry);
        } else {
            std::filesystem::copy_file(entry, destination_entry, std::filesystem::copy_options::overwrite_existing);
        }
    }
}

std::string toCppType(std::string type) {
    boost::trim(type);
    auto convert = [](const auto& type) {
        static std::unordered_map<std::string, std::string> cpp_type_table{
            {"bool", "bool"},
            {"i8", "int8_t"},
            {"u8", "uint8_t"},
            {"i16", "int16_t"},
            {"u16", "uint16_t"},
            {"i32", "int32_t"},
            {"u32", "uint32_t"},
            {"i64", "int64_t"},
            {"u64", "uint64_t"},
            {"f32", "float"},
            {"f64", "double"},
            {"string", "std::string"},
        };
        if (cpp_type_table.contains(type))
            return cpp_type_table[type];
        return type;
    };
    if (type.starts_with("map")) {
        auto extract_str = extract(type);
        auto types = extract(type) |
                     std::views::split(';') |
                     std::views::transform([](auto&& rng) {
                         return std::string(&*rng.begin(), std::ranges::distance(rng.begin(), rng.end()));
                     }) |
                     to<std::vector<std::string>>();
        if (types.size() != 2) {
            spdlog::error("invalid: [{}]", type);
            exit(1);
        }
        type = std::format("std::unordered_map<{}, {}>", convert(types.at(0)), convert(types.at(1)));
    }
    if (type.starts_with("vector"))
        type = std::format("std::vector<{}>", convert(extract(type)));
    if (type.starts_with("option"))
        type = std::format("std::optional<{}>", convert(extract(type)));
    return convert(type);
}

auto _snake(inja::Arguments& args) {
    return toSnakeCase(args.at(0)->get<std::string>());
}

auto _join(inja::Arguments& args) {
    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << toSnakeCase(json["name"]) << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_args(inja::Arguments& args) {
    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << json["type"].get<std::string>() << " " << json["name"].get<std::string>() << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _is_fundamental(inja::Arguments& args) {
    auto type = args.at(0)->get<std::string>();
    if (CPP_TYPE_TABLE.contains(type) || type == "std::string")
        return true;
    return false;
}

static std::unordered_map<std::string, std::string> ENUM_NAME_TABLE;

auto _format_args_to_const_ref(inja::Arguments& args) {
    auto convert = [](const auto& type, const auto& name) {
        if (CPP_TYPE_TABLE.contains(type))
            return std::format("{} {}", type, name);
        return std::format("const {}& {}", type, name);
    };
    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << convert(json["type"].get<std::string>(), json["name"].get<std::string>()) << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_args_type(inja::Arguments& args) {
    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << json["type"].get<std::string>() << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_args_name_and_move(inja::Arguments& args) {
    auto convert = [](const auto& type, const auto& name) {
        if (CPP_TYPE_TABLE.contains(type) || ENUM_NAME_TABLE.contains(type))
            return name;
        return std::format("std::move({})", name);
    };

    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << convert(json["type"].get<std::string>(), json["name"].get<std::string>()) << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_catch_move(inja::Arguments& args) {
    auto convert = [](const auto& type, const auto& name) {
        if (CPP_TYPE_TABLE.contains(type) || ENUM_NAME_TABLE.contains(type))
            return name;
        return std::format("{} = std::move({})", name, name);
    };

    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << convert(json["type"].get<std::string>(), json["name"].get<std::string>()) << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_args_name(inja::Arguments& args) {
    std::stringstream ss;
    for (auto& json : *args.at(0))
        ss << json["name"].get<std::string>() << ",";
    auto str = ss.str();
    str.pop_back();
    return str;
}

auto _format_move_or_not(inja::Arguments& args) {
    auto convert = [](const auto& type, const auto& name) {
        if (CPP_TYPE_TABLE.contains(type) || ENUM_NAME_TABLE.contains(type))
            return name;
        return std::format("std::move({})", name);
    };
    auto name = args.at(0)->get<std::string>();
    auto type = args.at(1)->get<std::string>();
    return convert(type, name);
}

auto _random(inja::Arguments& args) {
    auto min = args.at(0)->get<uint64_t>();
    auto max = args.at(1)->get<uint64_t>();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(min, max);
    return distrib(gen);
}

void formatCode(const std::string& file, const std::string& content) {
    {
        std::ofstream write(file);
        if (!write.is_open()) {
            spdlog::error("write {} error!!!", file);
            return;
        }
        write << content;
    }
    char buffer[2048] = {};
    std::string result;
    char* clang_format_path = std::getenv("FRPC_CLANG_FORMAT");
    auto command = fmt::format("{} {}", clang_format_path ? clang_format_path : "clang-format", file);
    spdlog::info("start format {}", command);
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        spdlog::error("Couldn't start command {}", command);
        return;
    }
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
        memset(buffer, 0x00, sizeof(buffer));
    }
    pclose(pipe);
    if (result.empty())
        return;
    std::ofstream write(file);
    if (!write.is_open()) {
        spdlog::error("open {} fail", file);
        return;
    }
    write << result;
}

auto sort(nlohmann::json json) {
    auto& enum_json = json["enum"];
    auto& struct_json = json["struct"];
    // auto interface_json = json["interface"];

    auto check = [&](const std::string& type) {
        if (CPP_TYPE_TABLE.contains(type) || type == "std::string")
            return true;
        for (auto& e_json : enum_json) {
            if (e_json["enum_name"].get<std::string>() == type)
                return true;
        }
        return false;
    };

    typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS> Graph;
    Graph g;
    boost::unordered_map<std::string, Graph::vertex_descriptor> vertex_map;

    std::unordered_map<std::string, nlohmann::json> struct_json_map;
    for (auto& j : struct_json) {
        auto struct_name = j["struct_name"].get<std::string>();
        struct_json_map.emplace(struct_name, std::move(j));
        vertex_map[struct_name] = boost::add_vertex(g);
    }
    for (auto& [struct_name, j] : struct_json_map) {
        auto& definitions = j["definitions"];
        for (auto& field : definitions) {
            auto type = field["type"].get<std::string>();
            if (check(type))
                continue;
            if (type.starts_with("std::unordered_map")) {
                auto extract_str = extract(type);
                auto types = extract(type) |
                             std::views::split(',') |
                             std::views::transform([](auto&& rng) {
                                 return std::string(&*rng.begin(), std::ranges::distance(rng.begin(), rng.end()));
                             }) |
                             to<std::vector<std::string>>();
                if (types.size() != 2) {
                    spdlog::error("invalid: [{}]", type);
                    exit(1);
                }
                boost::trim(types[0]);
                boost::trim(types[1]);
                if (!check(types[0]))
                    boost::add_edge(vertex_map[struct_name], vertex_map[types[0]], g);
                if (!check(types[1]))
                    boost::add_edge(vertex_map[struct_name], vertex_map[types[1]], g);
            } else if (type.starts_with("std::vector") || type.starts_with("std::option")) {
                auto extract_str = extract(type);
                boost::trim(extract_str);
                if (!check(extract_str))
                    boost::add_edge(vertex_map[struct_name], vertex_map[extract_str], g);
            } else {
                boost::add_edge(vertex_map[struct_name], vertex_map[type], g);
            }
        }
    }
    std::vector<Graph::vertex_descriptor> sorted_vertices;
    boost::topological_sort(g, std::back_inserter(sorted_vertices));
    struct_json.clear();
    for (auto it = sorted_vertices.begin(); it != sorted_vertices.end(); ++it) {
        auto it1 = std::ranges::find_if(vertex_map, [&](auto& p) {
            auto& [k, v] = p;
            return v == *it;
        });
        if (it1 != vertex_map.end()) {
            spdlog::debug("{}", it1->first);
            struct_json.emplace_back(std::move(struct_json_map[it1->first]));
        }
    }
    return json;
}

nlohmann::json parseYaml(const std::string& file) {
    spdlog::info("start parse: {}", file);
    YAML::Node config = YAML::LoadFile(file);
    nlohmann::json ast;
    ast["node"]["value"] = std::vector<nlohmann::json>{};
    std::string filename, namespace_str;
    if (!config["property"]) {
        spdlog::error("{} not contains property", file);
        exit(1);
    }
    for (const auto& kv : config) {
        nlohmann::json data;
        auto node_name = kv.first.as<std::string>();
        if ("property" == node_name) {
            namespace_str = kv.second["namespace"].as<std::string>();
            filename = kv.second["filename"].as<std::string>();
            ast["node"][node_name]["filename"] = filename;
            ast["node"][node_name]["namespace"] = namespace_str;
            if (kv.second["include"]) {
                std::filesystem::path p = file;
                auto include_yaml = kv.second["include"].as<std::vector<std::string>>();
                for (auto& yaml : include_yaml) {
                    auto in_ast = parseYaml(std::format("{}/{}", p.parent_path().string(), yaml));
                    for (auto& value : in_ast["node"]["value"])
                        ast["node"]["value"].emplace_back(std::move(value));
                }
            }
            continue;
        }
        data["node_name"] = node_name;
        data["property"]["filename"] = filename;
        data["property"]["namespace"] = namespace_str;
        auto type = config[node_name]["type"].as<std::string>();
        data["type"] = type;
        if (type == "struct") {
            data["struct_name"] = node_name;
            nlohmann::json definitions_json;
            for (auto struct_val : config[node_name]["definitions"]) {
                nlohmann::json j;
                j["name"] = struct_val.first.as<std::string>();
                j["type"] = toCppType(struct_val.second["type"].as<std::string>());
                if (struct_val.second["comment"])
                    j["comment"] = struct_val.second["comment"].as<std::string>();
                else
                    j["comment"] = "";
                definitions_json.emplace_back(std::move(j));
            }
            data["definitions"] = std::move(definitions_json);
        }
        if (type == "enum") {
            ENUM_NAME_TABLE.emplace(node_name, node_name);
            data["enum_name"] = node_name;
            data["value_type"] = toCppType(config[node_name]["value_type"].as<std::string>());
            nlohmann::json definitions_json;
            for (auto struct_val : config[node_name]["definitions"]) {
                nlohmann::json j;
                j["name"] = struct_val.first.as<std::string>();
                j["default"] = struct_val.second["default"].as<std::string>();
                if (struct_val.second["comment"])
                    j["comment"] = struct_val.second["comment"].as<std::string>();
                else
                    j["comment"] = "";
                definitions_json.emplace_back(std::move(j));
            }
            data["definitions"] = std::move(definitions_json);
        }
        if (type == "interface") {
            data["interface_name"] = node_name;
            data["pattern"] = config[node_name]["pattern"].as<std::string>();
            data["caller"] = config[node_name]["caller"].as<std::string>();
            data["callee"] = config[node_name]["callee"].as<std::string>();
            for (auto struct_val : config[node_name]["definitions"]) {
                nlohmann::json j;
                nlohmann::json in_out;
                j["func_name"] = struct_val.first.as<std::string>();
                for (auto val : struct_val.second) {
                    if (val.first.as<std::string>() == "web") {
                        j["web"] = yaml2json(val.second);
                        continue;
                    }
                    for (auto val1 : val.second) {
                        in_out["name"] = val1.first.as<std::string>();
                        in_out["type"] = toCppType(val1.second["type"].as<std::string>());
                        j[val.first.as<std::string>()].emplace_back(std::move(in_out));
                    }
                }
                data["definitions"].emplace_back(std::move(j));
            }
        }
        ast["node"]["value"].emplace_back(std::move(data));
    }
    return ast;
}

void process(nlohmann::json& ast) {
    auto filter = [&](const std::string& name) {
        auto json =
            ast["node"]["value"] |
            std::views::filter([&](nlohmann::json& j) {
                auto type = j["type"].get<std::string>();
                return type == name;
            }) |
            to<std::vector<nlohmann::json>>();
        return json;
    };
    nlohmann::json j;
    j["enum"] = filter("enum");
    j["struct"] = filter("struct");
    j["interface"] = filter("interface");
    ast["node"]["value"] = std::move(j);
}

inja::Environment initEnv() {
    inja::Environment env;
    env.set_trim_blocks(true);
    env.set_lstrip_blocks(true);
    env.add_callback("_snake", 1, _snake);
    env.add_callback("_join", 1, _join);
    env.add_callback("_format_args", 1, _format_args);
    env.add_callback("_format_args_to_const_ref", 1, _format_args_to_const_ref);
    env.add_callback("_format_args_name", 1, _format_args_name);
    env.add_callback("_format_args_type", 1, _format_args_type);
    env.add_callback("_format_args_name_and_move", 1, _format_args_name_and_move);
    env.add_callback("_format_catch_move", 1, _format_catch_move);
    env.add_callback("_format_move_or_not", 2, _format_move_or_not);
    env.add_callback("_random", 2, _random);
    env.add_callback("_is_fundamental", 1, _is_fundamental);
    return env;
}

int main(int argc, char** argv) {
    cmdline::parser a;
    a.add<std::string>("filename", 'f', "input yaml yaml file", true, "");
    a.add<std::string>("template", 't', "template directory", true, "");
    a.add<std::string>("output", 'o', "output directory", true, "");
    a.add<std::string>("lang", 'l', "language", false, "cpp");
    a.add<std::string>("web_template", 'w', "web template directory", false, "");
    a.add<bool>("auto_sort", 's', "automatically sort structural dependencies", false, false);
    a.add<bool>("debug", 'd', "open debug log", false, false);
    a.parse_check(argc, argv);

    auto filename = a.get<std::string>("filename");
    auto inja_template_dir = a.get<std::string>("template");
    auto output = a.get<std::string>("output");
    auto lang = a.get<std::string>("lang");
    auto web_template = a.get<std::string>("web_template");
    auto auto_sort = a.get<bool>("auto_sort");
    auto debug = a.get<bool>("debug");

    spdlog::info("filename: {}", filename);
    if (inja_template_dir.back() == '/')
        inja_template_dir.pop_back();
    auto inja_template = std::format("{}/ast.cpp.inja", inja_template_dir);
    if (debug)
        spdlog::set_level(spdlog::level::debug);
    spdlog::info("template: {}", inja_template);
    spdlog::info("output: {}", output);
    spdlog::info("lang: {}", lang);
    spdlog::info("auto_sort: {}", auto_sort);
    spdlog::info("debug: {}", debug);

    inja::Environment env = initEnv();

    nlohmann::json data = parseYaml(filename);
    process(data);
    if (auto_sort)
        data["node"]["value"] = sort(std::move(data["node"]["value"]));
    spdlog::debug("{}", data.dump(4));

    std::filesystem::create_directories(std::format("{}/include/impl", output));
    std::filesystem::create_directories(std::format("{}/include/data", output));
    // create impl coroutine.h
    auto create_file = [&env](const auto& temp_file, const auto& filename, auto& ast_json) {
        auto temp = env.parse_template(temp_file);
        std::string result = env.render(temp, ast_json);
        formatCode(filename, result);
    };
    create_file(std::format("{}/impl/coroutine.h.inja", inja_template_dir),
                std::format("{}/include/impl/coroutine.h", output), data);
    create_file(std::format("{}/impl/bi_channel.inja", inja_template_dir),
                std::format("{}/include/impl/bi_channel.h", output), data);
    create_file(std::format("{}/impl/uni_channel.inja", inja_template_dir),
                std::format("{}/include/impl/uni_channel.h", output), data);
    create_file(std::format("{}/impl/utils.inja", inja_template_dir),
                std::format("{}/include/impl/utils.h", output), data);
    create_file(std::format("{}/impl/monitor.inja", inja_template_dir),
                std::format("{}/include/impl/monitor.h", output), data);
    create_file(std::format("{}/impl/asio_context_pool.inja", inja_template_dir),
                std::format("{}/include/impl/asio_context_pool.h", output), data);
    create_file(std::format("{}/impl/to_string.inja", inja_template_dir),
                std::format("{}/include/impl/to_string.h", output), data);
    create_file(std::format("{}/impl/from_string.inja", inja_template_dir),
                std::format("{}/include/impl/from_string.h", output), data);
    for (auto& enum_json : data["node"]["value"]["enum"]) {
        nlohmann::json ast;
        auto enum_file_name = toSnakeCase(enum_json["enum_name"].get<std::string>());
        ast["value"] = enum_json;
        create_file(std::format("{}/data/enum.inja", inja_template_dir),
                    std::format("{}/include/data/{}.h", output, enum_file_name), ast);
    }
    for (auto& struct_json : data["node"]["value"]["struct"]) {
        nlohmann::json ast;
        auto struct_file_name = toSnakeCase(struct_json["struct_name"].get<std::string>());
        ast["value"] = struct_json;
        create_file(std::format("{}/data/struct.inja", inja_template_dir),
                    std::format("{}/include/data/{}.h", output, struct_file_name), ast);
    }
    // generate header file
    auto header_file = data["node"].at("property").at("filename").get<std::string>();
    create_file(inja_template, std::format("{}/include/{}", output, header_file), data);

    if (web_template.empty())
        return 0;

    // // generate bi web service
    std::filesystem::create_directories(std::format("{}/bi_web/src/", output));
    std::filesystem::create_directories(std::format("{}/bi_web/include/", output));
    std::filesystem::create_directories(std::format("{}/bi_web/config/", output));
    copy_directory(std::format("{}/include", output), std::format("{}/bi_web/include/", output));

    create_file(std::format("{}/bi/src/main.cpp.inja", web_template), std::format("{}/bi_web/src/main.cpp", output), data);
    auto bi_temp = env.parse_template(std::format("{}/bi/xmake.lua.inja", web_template));
    env.write(bi_temp, data, std::format("{}/bi_web/xmake.lua", output));

    bi_temp = env.parse_template(std::format("{}/bi/config/config.example.json.inja", web_template));
    env.write(bi_temp, data, std::format("{}/bi_web/config/config.example.json", output));

    return 0;
}
