#include <algorithm>
#include <cstdint>
#include <iostream>
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

std::string toCppType(std::string type) {
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

static std::unordered_map<std::string, std::string> CPP_TYPE_TABLE{
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
        if (CPP_TYPE_TABLE.contains(type))
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
        if (CPP_TYPE_TABLE.contains(type))
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
        if (CPP_TYPE_TABLE.contains(type))
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
    auto command = fmt::format("{} {}", "clang-format", file);
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
    std::ofstream write(file);
    if (!write.is_open()) {
        spdlog::error("open {} fail", file);
        return;
    }
    write << result;
}

auto sort(nlohmann::json json) {
    static std::unordered_map<std::string, std::string> base_type{
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
        {"std::string", "std::string"},
    };
    nlohmann::json enum_json, struct_json, interface_json;
    for (auto& j : json) {
        auto type = j["type"].get<std::string>();
        if (type == "enum") {
            enum_json.emplace_back(std::move(j));
        } else if (type == "struct") {
            struct_json.emplace_back(std::move(j));
        } else if (type == "interface") {
            interface_json.emplace_back(std::move(j));
        } else {
            spdlog::error("Unexpected type");
            exit(1);
        }
    }

    auto check = [&](const std::string& type) {
        if (base_type.contains(type))
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
            boost::trim(type);
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
    for (auto it = sorted_vertices.begin(); it != sorted_vertices.end(); ++it) {
        auto it1 = std::ranges::find_if(vertex_map, [&](auto& p) {
            auto& [k, v] = p;
            return v == *it;
        });
        if (it1 != vertex_map.end()) {
            spdlog::info("{}", it1->first);
            enum_json.emplace_back(std::move(struct_json_map[it1->first]));
        }
    }
    for (auto& j : interface_json) {
        enum_json.emplace_back(std::move(j));
    }
    return enum_json;
}

auto parseYaml(const std::string& file) {
    YAML::Node config = YAML::LoadFile(file);
    nlohmann::json ast;
    std::string filename, namespace_str;
    for (const auto& kv : config) {
        nlohmann::json data;
        auto node_name = kv.first.as<std::string>();
        if ("property" == node_name) {
            namespace_str = kv.second["namespace"].as<std::string>();
            filename = kv.second["filename"].as<std::string>();
            ast["node"][node_name]["filename"] = filename;
            ast["node"][node_name]["namespace"] = namespace_str;
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
                j["comment"] = struct_val.second["comment"].as<std::string>();
                definitions_json.emplace_back(std::move(j));
            }
            data["definitions"] = std::move(definitions_json);
        }
        if (type == "enum") {
            data["enum_name"] = node_name;
            data["value_type"] = toCppType(config[node_name]["value_type"].as<std::string>());
            nlohmann::json definitions_json;
            for (auto struct_val : config[node_name]["definitions"]) {
                nlohmann::json j;
                j["name"] = struct_val.first.as<std::string>();
                j["default"] = struct_val.second["default"].as<std::string>();
                j["comment"] = struct_val.second["comment"].as<std::string>();
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

int main(int argc, char** argv) {
    cmdline::parser a;
    a.add<std::string>("filename", 'f', "input yaml yaml file", true, "");
    a.add<std::string>("template", 't', "template directory", true, "");
    a.add<std::string>("output", 'o', "output directory", true, "");
    a.add<std::string>("lang", 'l', "language", false, "cpp");
    a.add<std::string>("web_template", 'w', "web template directory", false, "");
    a.add<bool>("auto_sort", 's', "automatically sort structural dependencies", false, false);
    a.parse_check(argc, argv);

    auto filename = a.get<std::string>("filename");
    auto injia_template = a.get<std::string>("template");
    auto output = a.get<std::string>("output");
    auto lang = a.get<std::string>("lang");
    auto web_template = a.get<std::string>("web_template");
    auto auto_sort = a.get<bool>("auto_sort");

    spdlog::info("filename: {}", filename);
    if (injia_template.back() == '/')
        injia_template.pop_back();
    injia_template = std::format("{}/ast.cpp.inja", injia_template);
    spdlog::info("template: {}", injia_template);
    spdlog::info("output: {}", output);
    spdlog::info("lang: {}", lang);
    spdlog::info("auto_sort: {}", auto_sort);

    nlohmann::json data = parseYaml(filename);
    if (auto_sort)
        data["node"]["value"] = sort(std::move(data["node"]["value"]));
    // spdlog::info("{}", data.dump(4));

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

    auto temp = env.parse_template(injia_template);
    std::string result = env.render(temp, data);

    std::filesystem::create_directories(output);
    auto f = std::format("{}/{}", output, data["node"].at("property").at("filename").get<std::string>());
    formatCode(f, result);

    if (web_template.empty())
        return 0;
    // generate bi web service
    std::filesystem::create_directories(std::format("{}/bi_web/src/", output));
    std::filesystem::create_directories(std::format("{}/bi_web/include/", output));
    std::filesystem::create_directories(std::format("{}/bi_web/config/", output));
    f = std::format("{}/bi_web/include/{}", output, data["node"].at("property").at("filename").get<std::string>());
    formatCode(f, result);

    auto bi_temp = env.parse_template(std::format("{}/bi/src/main.cpp.inja", web_template));
    result = env.render(bi_temp, data);
    formatCode(std::format("{}/bi_web/src/main.cpp", output), result);

    bi_temp = env.parse_template(std::format("{}/bi/xmake.lua.inja", web_template));
    env.write(bi_temp, data, std::format("{}/bi_web/xmake.lua", output));

    bi_temp = env.parse_template(std::format("{}/bi/config/config.example.json.inja", web_template));
    env.write(bi_temp, data, std::format("{}/bi_web/config/config.example.json", output));

    return 0;
}
