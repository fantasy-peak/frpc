#ifndef _UTILS_H_
#define _UTILS_H_

#include <ranges>
#include <regex>

#include <yaml-cpp/yaml.h>
#include <nlohmann/json.hpp>

class ScopeExit {
public:
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    template <typename Callable>
    explicit ScopeExit(Callable&& call)
        : m_call(std::forward<Callable>(call)) {
    }

    ~ScopeExit() {
        if (m_call)
            m_call();
    }

    void clear() {
        m_call = decltype(m_call)();
    }

private:
    std::function<void()> m_call;
};

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

inline bool isSnakeCase(const std::string& str) {
    for (char c : str) {
        if (!(islower(c) || c == '_')) {
            return false;
        }
    }
    if (str.front() == '_' || str.back() == '_') {
        return false;
    }
    size_t pos = str.find("__");
    if (pos != std::string::npos) {
        return false;
    }
    return true;
}

inline auto toSnakeCase(const std::string& input) {
    if (isSnakeCase(input))
        return input;
    std::string snake_case;
    bool last_was_upper = false;
    for (char c : input) {
        if (std::isupper(c)) {
            if (!snake_case.empty() && !last_was_upper) {
                snake_case.push_back('_');
            }
            snake_case.push_back(std::tolower(c));
            last_was_upper = true;
        } else {
            snake_case.push_back(c);
            last_was_upper = false;
        }
    }
    return snake_case;
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
