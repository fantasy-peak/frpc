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

    MSGPACK_DEFINE(name, type, test_one, test_two, test_map_one, test_map, test_vector, info)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BankInfo, name, type, test_one, test_two, test_map_one, test_map, test_vector, info)

inline std::string toString(const BankInfo& value) {
    std::string str = "BankInfo{";
    str += toString(value.name);
    str += ",";
    str += toString(value.type);
    str += ",";
    str += toString(value.test_one);
    str += ",";
    str += toString(value.test_two);
    str += ",";
    str += toString(value.test_map_one);
    str += ",";
    str += toString(value.test_map);
    str += ",";
    str += toString(value.test_vector);
    str += ",";
    str += toString(value.info);
    str += ",";
    str += "}";
    return str;
}

} // namespace fantasy

#endif // _FANTASY_BANK_INFO_H_
