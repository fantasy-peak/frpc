#ifndef _UTILS_H_
#define _UTILS_H_

#include <ranges>
#include <regex>

#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

inline auto extract(const std::string& input) {
    std::regex pattern("<(.*?)>");
    std::smatch matches;
    if (!std::regex_search(input, matches, pattern)) {
        spdlog::error("extracted [{}] error.", input);
        exit(1);
    }
    std::string extracted_string = matches[1].str();
    return extracted_string;
}

inline auto toSnakeCase(const std::string& s) {
    std::regex words_regex("[A-Z][a-z]+");
    auto words_begin = std::sregex_iterator(s.begin(), s.end(), words_regex);
    auto words_end = std::sregex_iterator();
    std::string name{};
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string match_str = match.str();
        std::transform(match_str.begin(), match_str.end(), match_str.begin(), ::tolower);
        auto z = i;
        if (++z == words_end)
            name += match_str;
        else
            name += (match_str + "_");
    }
    return name;
}

namespace detail {

inline nlohmann::json parse_scalar(const YAML::Node& node) {
    int i;
    double d;
    bool b;
    std::string s;

    if (YAML::convert<int>::decode(node, i))
        return i;
    if (YAML::convert<double>::decode(node, d))
        return d;
    if (YAML::convert<bool>::decode(node, b))
        return b;
    if (YAML::convert<std::string>::decode(node, s))
        return s;

    return nullptr;
}

} // namespace detail

[[nodiscard]] inline nlohmann::json yaml2json(const YAML::Node& root) {
    nlohmann::json j{};
    using namespace detail;
    switch (root.Type()) {
        case YAML::NodeType::Null:
            break;
        case YAML::NodeType::Scalar:
            return parse_scalar(root);
        case YAML::NodeType::Sequence:
            for (auto&& node : root)
                j.emplace_back(yaml2json(node));
            break;
        case YAML::NodeType::Map:
            for (auto&& it : root)
                j[it.first.as<std::string>()] = yaml2json(it.second);
            break;
        default:
            break;
    }
    return j;
}

[[nodiscard]] inline nlohmann::json yaml2json(const std::string& str) {
    YAML::Node root = YAML::Load(str);
    return yaml2json(root);
}

namespace detail {

template <typename C>
struct to_helper {};

template <typename Container, std::ranges::range R>
    requires std::convertible_to<std::ranges::range_value_t<R>, typename Container::value_type>
Container operator|(R&& r, to_helper<Container>) {
    return Container{r.begin(), r.end()};
}

} // namespace detail

template <std::ranges::range Container>
    requires(!std::ranges::view<Container>)
inline auto to() {
    return detail::to_helper<Container>{};
}

#endif // _UTILS_H_
