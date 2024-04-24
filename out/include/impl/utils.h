#ifndef _FRPC_UTILS_H_
#define _FRPC_UTILS_H_

#include <functional>
#include <string>

#include <uuid/uuid.h>
#include <msgpack.hpp>

namespace frpc {

template <class TObject>
inline msgpack::sbuffer pack(const TObject& object) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, object);
    return buffer;
}

template <class TObject>
inline TObject unpack(const void* data, size_t size) {
    auto handle = msgpack::unpack(reinterpret_cast<const char*>(data), size);
    auto value = handle.get().as<TObject>();
    return value;
}

struct ChannelConfig {
    std::size_t io_threads{1};
    zmq::socket_type socktype{zmq::socket_type::dealer};
    int32_t sendhwm{0};
    int32_t recvhwm{0};
    int32_t sendbuf{0};
    int32_t recvbuf{0};
    int32_t linger{2000};
    std::string addr{"tcp://127.0.0.1:5833"};
    bool bind{false};
    bool mandatory{false};
    bool tcp_keepalive{true};
    int tcp_keepalive_idle{60};
    int tcp_keepalive_cnt{3};
    int tcp_keepalive_intvl{5};
    bool probe{false};
    int16_t context_pool_size{1};
    std::size_t channel_size{50000};
};

inline std::string uniqueAddr() {
    uuid_t uuid;
    char s[37];
    uuid_generate_random(uuid);
    uuid_unparse(uuid, s);
    return "inproc://" + std::string(s);
}

template <typename>
struct Stream;

template <typename R, typename... Args>
struct Stream<R(Args...)> : std::function<R(Args...)> {
    template <typename F>
    Stream(F&& func, std::function<void()>&& close)
        : std::function<R(Args...)>(std::forward<F>(func))
        , m_close(std::move(close)) {
    }
    Stream(const Stream&) = delete;
    Stream(Stream&&) = default;
    Stream& operator=(const Stream&) = delete;
    Stream& operator=(Stream&&) = default;

    ~Stream() {
        if (m_close) {
            m_close();
        }
    }

    void close() {
        if (m_close) {
            m_close();
        }
        m_close = nullptr;
    }

private:
    std::function<void()> m_close;
};

} // namespace frpc

#endif // _FRPC_UTILS_H_
