#ifndef _FANTASY_BANK_INFO_H_
#define _FANTASY_BANK_INFO_H_

namespace fantasy {

struct BankInfo {
    std::string name;                                       // test Name
    TestType type;                                          // test Type
    int32_t test_one;                                       // test 1
    uint32_t test_two;                                      // test 2
    std::unordered_map<std::string, TestType> test_map_one; // test map
    std::unordered_map<bool, uint32_t> test_map;            // test map
    std::vector<std::string> test_vector;                   // test map
    Info info;                                              // Info
    frpc::DateTime date_time;                               // date time

    MSGPACK_DEFINE(name, type, test_one, test_two, test_map_one, test_map, test_vector, info, date_time)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BankInfo, name, type, test_one, test_two, test_map_one, test_map, test_vector, info, date_time)

inline std::string toString(const BankInfo& value) {
    std::ostringstream ss;
    ss << "BankInfo{" << value.name
       << "," << toString(value.type)
       << "," << value.test_one
       << "," << value.test_two
       << "," << toString(value.test_map_one)
       << "," << toString(value.test_map)
       << "," << toString(value.test_vector)
       << "," << toString(value.info)
       << "," << toString(value.date_time)
       << "}";
    return ss.str();
}

inline bool operator==(const BankInfo& lhs, const BankInfo& rhs) {
    return (lhs.name == rhs.name) &&
           (lhs.type == rhs.type) &&
           (lhs.test_one == rhs.test_one) &&
           (lhs.test_two == rhs.test_two) &&
           (lhs.test_map_one == rhs.test_map_one) &&
           (lhs.test_map == rhs.test_map) &&
           (lhs.test_vector == rhs.test_vector) &&
           (lhs.info == rhs.info) &&
           (lhs.date_time == rhs.date_time);
}

inline bool operator!=(const BankInfo& lhs, const BankInfo& rhs) {
    return !(lhs == rhs);
}

} // namespace fantasy

#endif // _FANTASY_BANK_INFO_H_
