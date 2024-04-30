#ifndef _FANTASY_INFO_H_
#define _FANTASY_INFO_H_

namespace fantasy {

struct Info {
    std::optional<std::string> name; // test Name

    MSGPACK_DEFINE(name)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Info, name)

inline std::string toString(const Info& value) {
    std::ostringstream ss;
    ss << "Info{" << toString(value.name)
       << "}";
    return ss.str();
}

inline bool operator==(const Info& lhs, const Info& rhs) {
    return (lhs.name == rhs.name);
}

inline bool operator!=(const Info& lhs, const Info& rhs) {
    return !(lhs == rhs);
}

} // namespace fantasy

#endif // _FANTASY_INFO_H_
