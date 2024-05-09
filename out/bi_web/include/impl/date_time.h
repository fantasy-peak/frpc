#ifndef _FRPC_DATE_TIME_H_
#define _FRPC_DATE_TIME_H_

#include <chrono>
#include <cstdint>

namespace frpc {

struct Date {
    Date() = default;
    Date(uint16_t year, uint8_t month, uint8_t day)
        : year(year)
        , month(month)
        , day(day) {
    }

    uint16_t year{};
    uint8_t month{};
    uint8_t day{};

    MSGPACK_DEFINE(year, month, day)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Date, year, month, day)

inline std::string toString(const Date& value) {
    std::ostringstream ss;
    ss << "Date"
       << " year=" << value.year << " month=" << static_cast<uint32_t>(value.month)
       << " day=" << static_cast<uint32_t>(value.day);
    return ss.str();
}

inline bool operator==(const Date& lhs, const Date& rhs) {
    return (lhs.year == rhs.year) && (lhs.month == rhs.month) && (lhs.day == rhs.day);
}

inline bool operator!=(const Date& lhs, const Date& rhs) {
    return !(lhs == rhs);
}

struct Time {
    Time() = default;
    Time(uint8_t hour, uint8_t minute, uint8_t second, uint32_t microsecond)
        : hour(hour)
        , minute(minute)
        , second(second)
        , microsecond(microsecond) {
    }

    uint8_t hour{};
    uint8_t minute{};
    uint8_t second{};
    uint32_t microsecond{};

    MSGPACK_DEFINE(hour, minute, second, microsecond)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Time, hour, minute, second, microsecond)

inline std::string toString(const Time& value) {
    std::ostringstream ss;
    ss << "Time"
       << " hour=" << static_cast<uint32_t>(value.hour) << " minute=" << static_cast<uint32_t>(value.minute)
       << " second=" << static_cast<uint32_t>(value.second) << " microsecond=" << value.microsecond;
    return ss.str();
}

inline bool operator==(const Time& lhs, const Time& rhs) {
    return (lhs.hour == rhs.hour) &&
           (lhs.minute == rhs.minute) &&
           (lhs.second == rhs.second) &&
           (lhs.microsecond == rhs.microsecond);
}

inline bool operator!=(const Time& lhs, const Time& rhs) {
    return !(lhs == rhs);
}

struct DateTime {
    DateTime() = default;
    DateTime(const Date& date, const Time& time)
        : date(date)
        , time(time) {
    }

    Date date{};
    Time time{};

    MSGPACK_DEFINE(date, time)
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(DateTime, date, time)

inline std::string toString(const DateTime& value) {
    std::ostringstream ss;
    ss << "DateTime"
       << " date=" << toString(value.date) << " time=" << toString(value.time);
    return ss.str();
}

inline bool operator==(const DateTime& lhs, const DateTime& rhs) {
    return (lhs.date == rhs.date) && (lhs.time == rhs.time);
}

inline bool operator!=(const DateTime& lhs, const DateTime& rhs) {
    return !(lhs == rhs);
}

inline DateTime getFrpcDateTime(std::chrono::system_clock::time_point now_time = std::chrono::system_clock::now()) {
    auto mic = std::chrono::duration_cast<std::chrono::microseconds>(now_time.time_since_epoch());
    auto microseconds = mic.count() % 1000000;
    auto time_tt = std::chrono::system_clock::to_time_t(now_time);
    struct tm work {};
    localtime_r(&time_tt, &work);
    Date d(1900 + work.tm_year, work.tm_mon + 1, work.tm_mday);
    Time t(work.tm_hour, work.tm_min, work.tm_sec, microseconds);
    return DateTime{d, t};
}

inline std::chrono::system_clock::time_point frpcDateTimeToTimePoint(const DateTime& date_time) {
    struct tm timeinfo {};
    timeinfo.tm_year = date_time.date.year - 1900;
    timeinfo.tm_mon = date_time.date.month - 1;
    timeinfo.tm_mday = date_time.date.day;
    timeinfo.tm_hour = date_time.time.hour;
    timeinfo.tm_min = date_time.time.minute;
    timeinfo.tm_sec = date_time.time.second;
    auto time_c = std::mktime(&timeinfo);
    return std::chrono::system_clock::from_time_t(time_c) + std::chrono::microseconds{date_time.time.microsecond};
}

template <bool millisec = false>
inline std::string fromFrpcDateTime(const DateTime& date_time, const std::string& format = "%Y-%m-%d %H:%M:%S") {
    struct tm timeinfo {};
    timeinfo.tm_year = date_time.date.year - 1900;
    timeinfo.tm_mon = date_time.date.month - 1;
    timeinfo.tm_mday = date_time.date.day;
    timeinfo.tm_hour = date_time.time.hour;
    timeinfo.tm_min = date_time.time.minute;
    timeinfo.tm_sec = date_time.time.second;
    std::stringstream ss;
    ss << std::put_time(&timeinfo, format.c_str());
    if constexpr (millisec) {
        auto millisecond = date_time.time.microsecond / 1000;
        ss << '.' << std::setw(3) << std::setfill('0') << millisecond;
    }
    return ss.str();
}

inline DateTime toFrpcDateTime(const std::string& date, const std::string& format = "%Y-%m-%d %H:%M:%S.") {
    struct tm tm {};
    std::stringstream ss(date);
    ss >> std::get_time(&tm, format.c_str());
    int milliseconds = 0;
    ss >> milliseconds;
    Date d(1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday);
    Time t(tm.tm_hour, tm.tm_min, tm.tm_sec, milliseconds * 1000);
    return DateTime{d, t};
}

} // namespace frpc

namespace std {

template <>
struct hash<frpc::Date> {
    size_t operator()(const frpc::Date& value) const {
        return std::hash<uint16_t>{}(value.year) ^ std::hash<uint8_t>{}(value.month) ^ std::hash<uint8_t>{}(value.day);
    }
};

template <>
struct hash<frpc::Time> {
    size_t operator()(const frpc::Time& value) const {
        return std::hash<uint8_t>{}(value.hour) ^
               std::hash<uint8_t>{}(value.minute) ^
               std::hash<uint8_t>{}(value.second) ^
               std::hash<uint32_t>{}(value.microsecond);
    }
};

template <>
struct hash<frpc::DateTime> {
    size_t operator()(const frpc::DateTime& value) const {
        return std::hash<frpc::Date>{}(value.date) ^ std::hash<frpc::Time>{}(value.time);
    }
};

} // namespace std

#endif // _FRPC_DATE_TIME_H_