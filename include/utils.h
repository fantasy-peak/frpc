#ifndef _UTILS_H_
#define _UTILS_H_

// #include <algorithm>
// #include <experimental/filesystem>
// #include <iostream>
// #include <regex>
// #include <string>
// #include <unordered_map>

// #include <spdlog/sinks/basic_file_sink.h>
// #include <spdlog/spdlog.h>

#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

// namespace easy_brpc {

// inline std::string ltrim(const std::string str) {
// 	auto s = str;
// 	s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
// 	return s;
// }

// inline std::string rtrim(const std::string str) {
// 	auto s = str;
// 	s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
// 	return s;
// }

// inline std::string trim(const std::string str) {
// 	return ltrim(rtrim(str));
// }

// inline std::string toUpper(std::string str) {
// 	std::transform(str.begin(), str.end(), str.begin(), ::toupper);
// 	return str;
// }

// inline std::string toLower(std::string str) {
// 	std::transform(str.begin(), str.end(), str.begin(), ::tolower);
// 	return str;
// }

// inline auto toSnakeCase(const std::string& s) {
// 	std::regex words_regex("[A-Z][a-z]+");
// 	auto words_begin = std::sregex_iterator(s.begin(), s.end(), words_regex);
// 	auto words_end = std::sregex_iterator();
// 	std::string name{};
// 	for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
// 		std::smatch match = *i;
// 		std::string match_str = match.str();
// 		std::transform(match_str.begin(), match_str.end(), match_str.begin(), ::tolower);
// 		auto z = i;
// 		if (++z == words_end)
// 			name += match_str;
// 		else
// 			name += (match_str + "_");
// 	}
// 	return name;
// }

// inline std::vector<std::string> split(const std::string& str, const char delimiter, bool include_delimiter = false) {
// 	size_t start = 0;
// 	size_t end = str.find_first_of(delimiter);
// 	std::vector<std::string> output;
// 	while (end <= std::string::npos) {
// 		if (start == str.size())
// 			break;
// 		if (include_delimiter)
// 			output.emplace_back(str.substr(start, end - start + 1));
// 		else
// 			output.emplace_back(str.substr(start, end - start));
// 		if (end == std::string::npos)
// 			break;
// 		start = end + 1;
// 		end = str.find_first_of(delimiter, start);
// 	}
// 	return output;
// }

// inline std::string GetDirectory(const std::string& path) {
// 	std::experimental::filesystem::path p(path);
// 	return (p.parent_path().string());
// }

// inline std::string remove(std::string str, char c = '/') {
// 	if (str.back() == c)
// 		str.erase(str.size() - 1);
// 	return str;
// }

// inline std::vector<std::string> process(std::string data) {
// 	std::replace(data.begin(), data.end(), ';', ' ');
// 	std::replace(data.begin(), data.end(), '<', ' ');
// 	std::replace(data.begin(), data.end(), '>', ' ');
// 	return split(data, ' ');
// }

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
