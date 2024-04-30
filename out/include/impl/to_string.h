#ifndef _FANTASY_TO_STRING_H_
#define _FANTASY_TO_STRING_H_

namespace fantasy {

template <typename T>
T fromString(const std::string&);

template <typename T>
inline std::enable_if_t<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, std::string> toString(T value) {
    return std::to_string(value);
}

inline std::string_view toString(bool value) {
    return value ? "true" : "false";
}

inline std::string toString(const std::string& value) {
    return value;
}

template <typename T>
inline std::string toString(const std::vector<T>& vector) {
    std::string str = "[";
    auto it = vector.begin();
    if (it != vector.end()) {
        str += toString(*it);
        ++it;
    }
    for (; it != vector.end(); ++it) {
        str += ",";
        str += toString(*it);
    }
    str += "]";
    return str;
}

template <typename K, typename V>
inline std::string toString(const std::unordered_map<K, V>& map) {
    std::string str = "{";
    auto it = map.begin();
    if (it != map.end()) {
        str += toString(it->first);
        str += "->";
        str += toString(it->second);
        ++it;
    }
    for (; it != map.end(); ++it) {
        str += ",";
        str += toString(it->first);
        str += "->";
        str += toString(it->second);
    }
    str += "}";
    return str;
}

template <typename T>
inline std::string toString(const std::optional<T>& opt) {
    std::ostringstream ss;
    if (opt) {
        if constexpr (std::is_fundamental_v<T> || std::is_same_v<T, std::string>) {
            ss << opt.value();
        } else {
            ss << toString(opt.value());
        }
    } else {
        ss << "(nullopt)";
    }
    return ss.str();
}

} // namespace fantasy

#endif //_FANTASY_TO_STRING_H_