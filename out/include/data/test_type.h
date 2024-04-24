namespace fantasy {

enum class TestType : int32_t {
    EnumOne = 0, // zero
    EnumTwo = 1, // one
};

NLOHMANN_JSON_SERIALIZE_ENUM(TestType, {
                                           {TestType::EnumOne, "EnumOne"},
                                           {TestType::EnumTwo, "EnumTwo"},
                                       })

inline std::string_view toString(const TestType value) {
    switch (value) {
        case TestType::EnumOne:
            return "0";
        case TestType::EnumTwo:
            return "1";
        default:
            return "???";
    }
}

template <>
inline TestType fromString<TestType>(const std::string& value) {
    if (value == "EnumOne")
        return TestType::EnumOne;
    if (value == "EnumTwo")
        return TestType::EnumTwo;
    throw std::bad_cast();
}

} // namespace fantasy

MSGPACK_ADD_ENUM(fantasy::TestType)
