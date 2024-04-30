#ifndef _FRPC_TO_STRING_H_
#define _FRPC_TO_STRING_H_

namespace std {

template <typename T>
inline std::string toString(const std::vector<T>& vec) {
    std::ostringstream ss;

    ss << "[";
    for (auto& i : vec) {
        if constexpr (std::is_fundamental_v<T> || std::is_same_v<T, std::string>) {
            if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
                ss << static_cast<int32_t>(i) << " ";
            else
                ss << i << ",";
        } else {
            ss << toString(i) << ",";
        }
    }
    ss << "]";

    return ss.str();
}

template <typename K, typename V>
inline std::string toString(const std::unordered_map<K, V>& map) {
    std::ostringstream ss;

    ss << "{";
    for (auto& [k, v] : map) {
        if constexpr (std::is_fundamental_v<K> || std::is_same_v<K, std::string>) {
            if constexpr (std::is_same_v<K, uint8_t> || std::is_same_v<K, int8_t>)
                ss << static_cast<int32_t>(k) << "=";
            else
                ss << k << "=";
        } else {
            ss << toString(k) << "=";
        }
        if constexpr (std::is_fundamental_v<V> || std::is_same_v<V, std::string>) {
            if constexpr (std::is_same_v<V, uint8_t> || std::is_same_v<V, int8_t>)
                ss << static_cast<int32_t>(v) << ",";
            else
                ss << v << ",";
        } else {
            ss << toString(v) << ",";
        }
    }
    ss << "}";

    return ss.str();
}

template <typename T>
inline std::string toString(const std::optional<T>& opt) {
    std::ostringstream ss;

    if (opt) {
        if constexpr (std::is_fundamental_v<T> || std::is_same_v<T, std::string>) {
            if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
                ss << static_cast<int32_t>(opt.value());
            else
                ss << opt.value();
        } else {
            ss << toString(opt.value());
        }
    } else {
        ss << "(nullopt)";
    }

    return ss.str();
}

} // namespace std

#endif //_FRPC_TO_STRING_H_
