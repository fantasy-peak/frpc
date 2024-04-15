#ifndef _FRPC_H_
#define _FRPC_H_

#include <any>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <variant>
#ifdef __cpp_impl_coroutine
#include <coroutine>
#endif
#include <optional>

#include <uuid/uuid.h>
#include <msgpack.hpp>
#include <zmq.hpp>
#include <zmq_addon.hpp>

#ifdef __cpp_impl_coroutine
#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace frpc {

class ContextPool final {
public:
    ContextPool(std::size_t pool_size)
        : m_next_io_context(0) {
        if (pool_size == 0)
            throw std::runtime_error("ContextPool size is 0");
        for (std::size_t i = 0; i < pool_size; ++i) {
            auto io_context_ptr = std::make_shared<asio::io_context>();
            m_io_contexts.emplace_back(io_context_ptr);
            m_work.emplace_back(
                asio::require(io_context_ptr->get_executor(), asio::execution::outstanding_work.tracked));
        }
    }

    void start() {
        for (auto& context : m_io_contexts)
            m_threads.emplace_back(std::jthread([&] {
                context->run();
            }));
    }

    void stop() {
        for (auto& context_ptr : m_io_contexts)
            context_ptr->stop();
    }

    asio::io_context& getIoContext() {
        size_t index = m_next_io_context.fetch_add(1, std::memory_order_relaxed);
        return *m_io_contexts[index % m_io_contexts.size()];
    }

private:
    std::vector<std::shared_ptr<asio::io_context>> m_io_contexts;
    std::list<asio::any_io_executor> m_work;
    std::atomic_uint64_t m_next_io_context;
    std::vector<std::jthread> m_threads;
};

} // namespace frpc
#endif

namespace frpc {

#define FRPC_ERROR_FORMAT(message) [](const std::string& info) { \
    std::stringstream ss;                                        \
    ss << __FILE__ << ":" << __LINE__ << " " << info;            \
    return ss.str();                                             \
}(message)

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

template <typename T>
T fromString(const std::string&);

template <typename T>
inline std::enable_if_t<std::is_arithmetic<T>::value && !std::is_same<T, bool>::value, std::string> toString(T value) {
    return std::to_string(value);
}

inline std::string toString(bool value) {
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

inline std::string uuid() {
    uuid_t uuid;
    char uuid_str[37];
    uuid_generate_random(uuid);
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
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

namespace frpc {

inline std::string_view getEventName(uint16_t event_type) {
    switch (event_type) {
        case ZMQ_EVENT_CONNECTED:
            return "Connected";
        case ZMQ_EVENT_CONNECT_DELAYED:
            return "Connect Delayed";
        case ZMQ_EVENT_CONNECT_RETRIED:
            return "Connect Retried";
        case ZMQ_EVENT_LISTENING:
            return "Listening";
        case ZMQ_EVENT_BIND_FAILED:
            return "Bind Failed";
        case ZMQ_EVENT_ACCEPTED:
            return "Accepted";
        case ZMQ_EVENT_ACCEPT_FAILED:
            return "Accept Failed";
        case ZMQ_EVENT_CLOSED:
            return "Closed";
        case ZMQ_EVENT_CLOSE_FAILED:
            return "Close Failed";
        case ZMQ_EVENT_DISCONNECTED:
            return "Disconnected";
        case ZMQ_EVENT_MONITOR_STOPPED:
            return "Monitor Stopped";
        case ZMQ_EVENT_ALL:
            return "Monitor All";
        default:
            break;
    }
    return "Unknown";
}

class Monitor {
public:
    Monitor(zmq::context_t& context, zmq::socket_t& socket)
        : m_context(context)
        , m_socket(socket)
        , m_endpoint(uniqueAddr()) {
    }

    virtual ~Monitor() {
        stop();
    }

    void start(std::function<void(std::optional<std::tuple<zmq_event_t, std::string>>)> call_back, int events = ZMQ_EVENT_ALL) {
        m_monitor_socket = std::make_unique<zmq::socket_t>(m_context, zmq::socket_type::pair);

        auto debug = std::getenv("DEBUG_EVENTS");
        if (debug)
            events = std::stoi(debug);

        int rc = zmq_socket_monitor(static_cast<void*>(m_socket), m_endpoint.c_str(), events);
        if (rc != 0) {
            throw ::zmq::error_t();
        }
        m_monitor_socket->connect(m_endpoint);
        m_monitor_call_back = std::move(call_back);

        m_thread = std::thread([&] {
            std::vector<zmq::pollitem_t> polls = {
                {static_cast<void*>(*(m_monitor_socket)), 0, ZMQ_POLLIN, 0},
            };

            while (m_activate.load(std::memory_order_acquire)) {
                zmq::poll(polls.data(), polls.size(), std::chrono::seconds(1));
                if (polls[0].revents & ZMQ_POLLIN)
                    m_monitor_call_back(getEvent());
            }
        });
    }

    auto& socket() {
        return m_monitor_socket;
    }

    void stop() {
        m_activate.store(false, std::memory_order_release);
        if (m_thread.joinable())
            m_thread.join();

        auto socket_ptr = static_cast<void*>(m_socket);
        if (socket_ptr)
            zmq_socket_monitor(socket_ptr, nullptr, 0);
    }

    std::optional<std::tuple<zmq_event_t, std::string>> getEvent() {
        zmq::message_t event_msg;
        if (!m_monitor_socket->recv(event_msg))
            return {};
        zmq::message_t addr_msg;
        if (!m_monitor_socket->recv(addr_msg))
            return {};

        zmq_event_t event;
        const char* data = static_cast<const char*>(event_msg.data());
        std::memcpy(&event.event, data, sizeof(uint16_t));
        std::memcpy(&event.value, data + sizeof(uint16_t), sizeof(int32_t));
        return std::make_tuple(event, std::string(static_cast<const char*>(addr_msg.data()), addr_msg.size()));
    }

private:
    zmq::context_t& m_context;
    zmq::socket_t& m_socket;
    std::string m_endpoint;
    std::unique_ptr<zmq::socket_t> m_monitor_socket;
    std::atomic<bool> m_activate{true};
    std::thread m_thread;
    std::function<void(std::optional<std::tuple<zmq_event_t, std::string>>)> m_monitor_call_back{nullptr};
};

} // namespace frpc

namespace frpc {

class BiChannel final {
public:
    BiChannel(const ChannelConfig& config,
              std::function<void(std::string)> error,
              std::function<void(std::vector<zmq::message_t>&)> cb)
        : m_context(config.io_threads)
        , m_socket(m_context, config.socktype)
        , m_send(m_context, zmq::socket_type::push)
        , m_recv(m_context, zmq::socket_type::pull)
        , m_error(std::move(error))
        , m_cb(std::move(cb)) {
        m_socket.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket.set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket.set(zmq::sockopt::tcp_keepalive, 1);
            m_socket.set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket.set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket.set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.probe) {
            m_socket.set(zmq::sockopt::probe_router, 1);
        }
        if (config.bind) {
            if (config.socktype == zmq::socket_type::router) {
                if (config.mandatory)
                    m_socket.set(zmq::sockopt::router_mandatory, 0);
                else
                    m_socket.set(zmq::sockopt::router_mandatory, 1);
            }
            m_socket.bind(config.addr);
        } else {
            m_socket.connect(config.addr);
        }
        auto addr = uniqueAddr();
        m_recv.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_recv.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_recv.bind(addr);

        m_send.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_send.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_send.set(zmq::sockopt::linger, config.linger);

        m_send.connect(addr);
    }
    ~BiChannel() {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }

    template <typename T>
    void send(T&& snd_msgs) {
        std::lock_guard lk(m_mutex);
        if (!zmq::send_multipart(m_send, std::forward<decltype(snd_msgs)>(snd_msgs))) {
            m_error(FRPC_ERROR_FORMAT("send error!!!"));
        }
    }

    template <typename T>
    void send(T&& snd_msgs,
              std::chrono::milliseconds timeout,
              std::function<void()> cb) {
        std::lock_guard lk(m_mutex);
        if (!zmq::send_multipart(m_send, std::forward<decltype(snd_msgs)>(snd_msgs))) {
            m_error(FRPC_ERROR_FORMAT("send error!!!"));
        }
        auto timeout_point = std::chrono::system_clock::now() + timeout;
        m_timeout_task.emplace(timeout_point, std::move(cb));
    }

    void start() {
        m_running = true;
        m_thread = std::thread([this] {
            std::vector<zmq::pollitem_t> items{
                {static_cast<void*>(m_socket), 0, ZMQ_POLLIN | ZMQ_POLLERR, 0},
                {static_cast<void*>(m_recv), 0, ZMQ_POLLIN | ZMQ_POLLERR, 0},
            };
            std::chrono::milliseconds interval(200);
            std::multimap<std::chrono::system_clock::time_point, std::function<void()>> timeout_task;
            while (m_running.load()) {
                zmq::poll(items, interval);
                if (items[0].revents & ZMQ_POLLIN) {
                    std::vector<zmq::message_t> recv_msgs;
                    auto ret = zmq::recv_multipart(m_socket, std::back_inserter(recv_msgs));
                    if (!ret) {
                        m_error(FRPC_ERROR_FORMAT("zmq::recv_multipart error!!!"));
                        break;
                    }
                    m_cb(recv_msgs);
                }
                if (items[1].revents & ZMQ_POLLIN) {
                    try {
                        std::vector<zmq::message_t> recv_msgs;
                        auto ret = zmq::recv_multipart(m_recv, std::back_inserter(recv_msgs));
                        if (!ret) {
                            m_error(FRPC_ERROR_FORMAT("recv zmq::recv_multipart error!!!"));
                            break;
                        }
                        if (!zmq::send_multipart(m_socket, recv_msgs)) {
                            m_error(FRPC_ERROR_FORMAT("zmq::send_multipart error!!!"));
                            break;
                        }
                    } catch (const zmq::error_t& e) {
                        m_error(FRPC_ERROR_FORMAT(std::string{"BiChannel error "} + e.what()));
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    if (!m_timeout_task.empty()) {
                        timeout_task.merge(m_timeout_task);
                        m_timeout_task.clear();
                    }
                }
                auto now = std::chrono::system_clock::now();
                while (!timeout_task.empty() && timeout_task.begin()->first <= now) {
                    (timeout_task.begin()->second)();
                    timeout_task.erase(timeout_task.begin());
                }
            }
        });
    }

    auto& socket() {
        return m_socket;
    }

    auto& context() {
        return m_context;
    }

private:
    zmq::context_t m_context;
    zmq::socket_t m_socket;
    zmq::socket_t m_send;
    zmq::socket_t m_recv;
    std::function<void(std::string)> m_error;
    std::function<void(std::vector<zmq::message_t>&)> m_cb;
    std::thread m_thread;
    std::atomic_bool m_running{false};
    std::mutex m_mutex;
    std::multimap<std::chrono::system_clock::time_point, std::function<void()>> m_timeout_task;
};

} // namespace frpc

namespace frpc {

struct UniChannel final {
public:
    UniChannel(const ChannelConfig& config,
               std::function<void(std::vector<zmq::message_t>&)> cb,
               std::function<void(std::string)> error)
        : m_context(std::make_shared<zmq::context_t>(config.io_threads))
        , m_socket(*m_context, config.socktype)
        , m_cb(std::move(cb))
        , m_error(std::move(error)) {
        m_socket.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket.set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket.set(zmq::sockopt::tcp_keepalive, 1);
            m_socket.set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket.set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket.set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.socktype == zmq::socket_type::sub)
            m_socket.set(zmq::sockopt::subscribe, "");
        if (config.bind)
            m_socket.bind(config.addr);
        else
            m_socket.connect(config.addr);
    }

    UniChannel(std::shared_ptr<zmq::context_t> context,
               const ChannelConfig& config,
               std::function<void(std::vector<zmq::message_t>&)> cb,
               std::function<void(std::string)> error)
        : m_context(std::move(context))
        , m_socket(*m_context, config.socktype)
        , m_cb(std::move(cb))
        , m_error(std::move(error)) {
        m_socket.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket.set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket.set(zmq::sockopt::tcp_keepalive, 1);
            m_socket.set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket.set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket.set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.socktype == zmq::socket_type::sub)
            m_socket.set(zmq::sockopt::subscribe, "");
        if (config.bind)
            m_socket.bind(config.addr);
        else
            m_socket.connect(config.addr);
    }

    ~UniChannel() {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }

    void start() {
        m_running = true;
        m_thread = std::thread([this] {
            std::vector<zmq::pollitem_t> items{
                {static_cast<void*>(m_socket), 0, ZMQ_POLLIN | ZMQ_POLLERR, 0},
            };
            std::chrono::milliseconds interval(200);
            while (m_running.load()) {
                zmq::poll(items, interval);
                if (items[0].revents & ZMQ_POLLIN) {
                    std::vector<zmq::message_t> recv_msgs;
                    auto ret = zmq::recv_multipart(m_socket, std::back_inserter(recv_msgs));
                    if (!ret) {
                        m_error(FRPC_ERROR_FORMAT("zmq::recv_multipart error!!!"));
                        break;
                    }
                    m_cb(recv_msgs);
                }
            }
        });
    }

    auto& context() {
        return *m_context;
    }

    auto& socket() {
        return m_socket;
    }

private:
    std::shared_ptr<zmq::context_t> m_context;
    zmq::socket_t m_socket;
    std::function<void(std::vector<zmq::message_t>&)> m_cb;
    std::function<void(std::string)> m_error;
    std::atomic_bool m_running;
    std::thread m_thread;
};

} // namespace frpc

namespace frpc {

enum class TestType : int32_t {
    EnumOne = 0, // zero
    EnumTwo = 1, // one
};

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

} // namespace frpc

MSGPACK_ADD_ENUM(frpc::TestType)

namespace frpc {

struct Info {
    std::string name; // test Name

    MSGPACK_DEFINE(name)
};

inline std::string toString(const Info& value) {
    std::string str = "Info{";
    str += toString(value.name);
    str += ",";
    str += "}";
    return str;
}

} // namespace frpc

namespace frpc {

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

} // namespace frpc

namespace frpc {

enum class HelloWorldClientHelloWorldServer {
    hello_world,

};

} // namespace frpc

MSGPACK_ADD_ENUM(frpc::HelloWorldClientHelloWorldServer)

namespace frpc {

class HelloWorldClient final {
public:
    HelloWorldClient(const ChannelConfig& config,
                     std::function<void(std::string)> error)
        : m_channel(std::make_unique<BiChannel>(config, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
    }

    void start() {
        m_channel->start();
    }

    auto& socket() {
        return m_channel->socket();
    }

    auto& context() {
        return m_channel->context();
    }

    void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, HelloWorldClientHelloWorldServer::hello_world);
        auto buffer = pack<decltype(header)>(std::move(header));
        auto packet = pack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(bank_info), std::move(bank_name), blance, std::move(date)));

        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(buffer.data(), buffer.size()));
        snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
        {
            std::lock_guard lk(m_mtx);
            m_cb.emplace(req_id, std::move(cb));
        }
        m_channel->send(std::move(snd_bufs));
    }

    void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date,
                     std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb,
                     std::chrono::milliseconds timeout,
                     std::function<void()> timeout_cb) {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, HelloWorldClientHelloWorldServer::hello_world);
        auto buffer = pack<decltype(header)>(std::move(header));
        auto packet = pack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(bank_info), std::move(bank_name), blance, std::move(date)));

        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(buffer.data(), buffer.size()));
        snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
        {
            std::lock_guard lk(m_mtx);
            m_cb.emplace(req_id, std::move(cb));
            m_timeout_cb.emplace(req_id, std::move(timeout_cb));
        }
        m_channel->send(std::move(snd_bufs),
                        std::move(timeout),
                        [this, req_id]() mutable {
                            std::unique_lock lk(m_mtx);
#if __cplusplus >= 202302L
                            if (!m_cb.contains(req_id) || !m_timeout_cb.contains(req_id))
                                return;
#else
            if (m_cb.find(req_id) == m_cb.end() || m_timeout_cb.find(req_id) == m_timeout_cb.end())
                return;
#endif
                            auto cb = std::move(m_timeout_cb[req_id]);
                            m_timeout_cb.erase(req_id);
                            m_cb.erase(req_id);
                            lk.unlock();
                            cb();
                        });
    }
#ifdef __cpp_impl_coroutine
    template <asio::completion_token_for<void(std::string, Info, uint64_t, std::optional<std::string>)> CompletionToken>
    auto hello_world_coro(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(std::string, Info, uint64_t, std::optional<std::string>)>(
            [this]<typename Handler>(Handler&& handler, BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date) mutable {
                auto handler_ptr = std::make_shared<Handler>(std::move(handler));
                this->hello_world(
                    std::move(bank_info), std::move(bank_name), blance, std::move(date),
                    [handler_ptr = std::move(handler_ptr)](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                        auto ex = asio::get_associated_executor(*handler_ptr);
                        asio::post(ex, [reply = std::move(reply), info = std::move(info), count, date = std::move(date), handler_ptr = std::move(handler_ptr)]() mutable -> void {
                            (*handler_ptr)(std::move(reply), std::move(info), count, std::move(date));
                        });
                    });
            },
            token,
            std::move(bank_info), std::move(bank_name), blance, std::move(date));
    }

    template <asio::completion_token_for<void(std::optional<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>)> CompletionToken>
    auto hello_world_coro(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::chrono::milliseconds timeout, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(std::optional<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>)>(
            [this]<typename Handler>(Handler&& handler, BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, auto timeout) mutable {
                auto handler_ptr = std::make_shared<Handler>(std::move(handler));
                this->hello_world(
                    std::move(bank_info), std::move(bank_name), blance, std::move(date),
                    [handler_ptr](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                        auto ex = asio::get_associated_executor(*handler_ptr);
                        asio::post(ex, [reply = std::move(reply), info = std::move(info), count, date = std::move(date), handler_ptr = std::move(handler_ptr)]() mutable -> void {
                            (*handler_ptr)(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                        });
                    },
                    std::move(timeout),
                    [handler_ptr] {
                        auto ex = asio::get_associated_executor(*handler_ptr);
                        asio::post(ex, [=, handler_ptr = std::move(handler_ptr)]() mutable -> void {
                            (*handler_ptr)(std::nullopt);
                        });
                    });
            },
            token,
            std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(timeout));
    }
#endif

    static auto create(ChannelConfig& config, std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<HelloWorldClient>(config, std::move(error));
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("Illegal response packet"));
            return;
        }
        try {
            auto [req_id, req_type] = unpack<std::tuple<uint64_t, HelloWorldClientHelloWorldServer>>(recv_bufs[0].data(), recv_bufs[0].size());
            switch (req_type) {
                case HelloWorldClientHelloWorldServer::hello_world: {
                    auto [reply, info, count, date] = unpack<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::unique_lock lk(m_mtx);
#if __cplusplus >= 202302L
                    if (!m_cb.contains(req_id))
                        break;
#else
                    if (m_cb.find(req_id) == m_cb.end())
                        break;
#endif
                    auto cb = std::move(m_cb[req_id]);
                    m_cb.erase(req_id);
                    lk.unlock();
                    auto callback = std::any_cast<std::function<void(std::string, Info, uint64_t, std::optional<std::string>)>>(cb);
                    callback(std::move(reply), std::move(info), count, std::move(date));
                    break;
                }
                default:
                    m_error(FRPC_ERROR_FORMAT("error type"));
            }
        } catch (const msgpack::type_error& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::bad_any_cast& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::exception& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        }
    }

    std::unique_ptr<BiChannel> m_channel;
    std::function<void(std::string)> m_error;
    std::mutex m_mtx;
    std::unordered_map<uint64_t, std::any> m_cb;
    std::unordered_map<uint64_t, std::function<void()>> m_timeout_cb;
    std::atomic_uint64_t m_req_id{0};
};

struct HelloWorldServerHandler {
    virtual void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) = 0;
};

struct CoroHelloWorldServerHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) = 0;
#else
    virtual void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) = 0;
#endif
};

class HelloWorldServer final {
public:
    HelloWorldServer(const ChannelConfig& config,
                     std::variant<std::shared_ptr<CoroHelloWorldServerHandler>, std::shared_ptr<HelloWorldServerHandler>> handler,
                     std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<BiChannel>(config, error, [this](std::vector<zmq::message_t>& recv_bufs) mutable {
            if (recv_bufs.size() != 3) {
                m_error(FRPC_ERROR_FORMAT("BiChannel recv illegal request packet"));
                return;
            }
            try {
                auto [req_id, req_type] = unpack<std::tuple<uint64_t, HelloWorldClientHelloWorldServer>>(recv_bufs[1].data(), recv_bufs[1].size());
                switch (req_type) {
                    case HelloWorldClientHelloWorldServer::hello_world: {
                        auto tp = unpack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(recv_bufs[2].data(), recv_bufs[2].size());
                        std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> out = [ptr = std::make_shared<std::vector<zmq::message_t>>(std::move(recv_bufs)), this](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                            auto& snd_bufs = *ptr;
                            auto packet = pack<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                            snd_bufs[2] = zmq::message_t(packet.data(), packet.size());
                            m_channel->send(std::move(snd_bufs));
                        };
                        std::visit([&](auto&& arg) mutable {
                            auto& [bank_info, bank_name, blance, date] = tp;
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldServerHandler>>) {
                                arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out));
                            } else {
#ifdef __cpp_impl_coroutine
                                asio::co_spawn(
                                    m_pool_ptr->getIoContext(),
                                    arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out)),
                                    asio::detached);
#else
                                arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out));
#endif
                            }
                        },
                                   m_handler);
                        break;
                    }
                    default:
                        m_error(FRPC_ERROR_FORMAT("error type"));
                }
            } catch (const msgpack::type_error& error) {
                m_error(FRPC_ERROR_FORMAT(error.what()));
            } catch (const std::bad_any_cast& error) {
                m_error(FRPC_ERROR_FORMAT(error.what()));
            } catch (const std::exception& error) {
                m_error(FRPC_ERROR_FORMAT(error.what()));
            }
        });
    }
    ~HelloWorldServer() {
#ifdef __cpp_impl_coroutine
        if (m_pool_ptr)
            m_pool_ptr->stop();
#endif
    }

    auto& socket() {
        return m_channel->socket();
    }

    auto& context() {
        return m_channel->context();
    }

    void start() {
        m_channel->start();
    }

    static auto create(ChannelConfig& config,
                       std::variant<std::shared_ptr<CoroHelloWorldServerHandler>, std::shared_ptr<HelloWorldServerHandler>> handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<HelloWorldServer>(config, std::move(handler), std::move(error));
    }

private:
    std::variant<std::shared_ptr<CoroHelloWorldServerHandler>, std::shared_ptr<HelloWorldServerHandler>> m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<BiChannel> m_channel;
    std::mutex m_mtx;
#ifdef __cpp_impl_coroutine
    std::unique_ptr<ContextPool> m_pool_ptr;
#endif
};

} // namespace frpc

namespace frpc {

enum class HelloWorldSenderHelloWorldReceiver {
    hello_world,
    notice,

};

inline std::string_view toString(const HelloWorldSenderHelloWorldReceiver value) {
    switch (value) {
        case HelloWorldSenderHelloWorldReceiver::hello_world:
            return "hello_world";
        case HelloWorldSenderHelloWorldReceiver::notice:
            return "notice";
        default:
            return "???";
    }
}

template <>
inline HelloWorldSenderHelloWorldReceiver fromString<HelloWorldSenderHelloWorldReceiver>(const std::string& value) {
    if (value == "hello_world")
        return HelloWorldSenderHelloWorldReceiver::hello_world;
    if (value == "notice")
        return HelloWorldSenderHelloWorldReceiver::notice;
    throw std::bad_cast();
}

} // namespace frpc

MSGPACK_ADD_ENUM(frpc::HelloWorldSenderHelloWorldReceiver)

namespace frpc {

struct HelloWorldReceiverHandler {
    virtual void hello_world(std::string in) = 0;
    virtual void notice(int32_t in, std::string info) = 0;
};

struct CoroHelloWorldReceiverHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(std::string in) = 0;
#else
    virtual void hello_world(std::string in) = 0;
#endif
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> notice(int32_t in, std::string info) = 0;
#else
    virtual void notice(int32_t in, std::string info) = 0;
#endif
};

class HelloWorldReceiver final {
public:
    HelloWorldReceiver(const ChannelConfig& config,
                       std::variant<std::shared_ptr<CoroHelloWorldReceiverHandler>, std::shared_ptr<HelloWorldReceiverHandler>> handler,
                       std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<UniChannel>(config, [this](auto& recv) mutable {
            dispatch(recv);
        },
                                                 error);
    }
    HelloWorldReceiver(const ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context,
                       std::variant<std::shared_ptr<CoroHelloWorldReceiverHandler>, std::shared_ptr<HelloWorldReceiverHandler>> handler,
                       std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<UniChannel>(context, config, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        },
                                                 error);
    }
    ~HelloWorldReceiver() {
#ifdef __cpp_impl_coroutine
        if (m_pool_ptr)
            m_pool_ptr->stop();
#endif
    }

    void start() {
        m_channel->start();
    }

    auto& socket() {
        return m_channel->socket();
    }

    auto& context() {
        return m_channel->context();
    }

    static auto create(ChannelConfig& config,
                       std::variant<std::shared_ptr<CoroHelloWorldReceiverHandler>, std::shared_ptr<HelloWorldReceiverHandler>> handler,
                       std::function<void(std::string)> error) {
        if ((config.socktype != zmq::socket_type::sub) && config.socktype != zmq::socket_type::pull)
            config.socktype = zmq::socket_type::sub;
        if (config.socktype == zmq::socket_type::sub)
            config.bind = false;
        return std::make_unique<HelloWorldReceiver>(config, std::move(handler), std::move(error));
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("Illegal response packet"));
            return;
        }
        try {
            auto req_type = unpack<HelloWorldSenderHelloWorldReceiver>(recv_bufs[0].data(), recv_bufs[0].size());
            switch (req_type) {
                case HelloWorldSenderHelloWorldReceiver::hello_world: {
                    auto tp = unpack<std::tuple<std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::visit(
                        [&](auto&& arg) {
                            auto& [in] = tp;
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldReceiverHandler>>) {
                                arg->hello_world(std::move(in));
                            } else {
#ifdef __cpp_impl_coroutine
                                asio::co_spawn(
                                    m_pool_ptr->getIoContext(),
                                    arg->hello_world(std::move(in)),
                                    asio::detached);
#else
                                arg->hello_world(std::move(in));
#endif
                            }
                        },
                        m_handler);
                    break;
                }
                case HelloWorldSenderHelloWorldReceiver::notice: {
                    auto tp = unpack<std::tuple<int32_t, std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::visit(
                        [&](auto&& arg) {
                            auto& [in, info] = tp;
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldReceiverHandler>>) {
                                arg->notice(in, std::move(info));
                            } else {
#ifdef __cpp_impl_coroutine
                                asio::co_spawn(
                                    m_pool_ptr->getIoContext(),
                                    arg->notice(in, std::move(info)),
                                    asio::detached);
#else
                                arg->notice(in, std::move(info));
#endif
                            }
                        },
                        m_handler);
                    break;
                }
                default:
                    m_error(FRPC_ERROR_FORMAT("error type"));
            }
        } catch (const msgpack::type_error& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::exception& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        }
    }

    std::variant<std::shared_ptr<CoroHelloWorldReceiverHandler>, std::shared_ptr<HelloWorldReceiverHandler>> m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<UniChannel> m_channel;
#ifdef __cpp_impl_coroutine
    std::unique_ptr<ContextPool> m_pool_ptr;
#endif
};

class HelloWorldSender final {
public:
    HelloWorldSender(const ChannelConfig& config)
        : m_context(std::make_shared<zmq::context_t>(config.io_threads))
        , m_socket(*m_context, config.socktype) {
        m_socket.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket.set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket.set(zmq::sockopt::tcp_keepalive, 1);
            m_socket.set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket.set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket.set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.bind)
            m_socket.bind(config.addr);
        else
            m_socket.connect(config.addr);
    }

    HelloWorldSender(const ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context)
        : m_context(context)
        , m_socket(*m_context, config.socktype) {
        if (config.tcp_keepalive) {
            m_socket.set(zmq::sockopt::tcp_keepalive, 1);
            m_socket.set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket.set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket.set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.bind)
            m_socket.bind(config.addr);
        else
            m_socket.connect(config.addr);
    }

    ~HelloWorldSender() {
    }

    static auto create(ChannelConfig& config) {
        if ((config.socktype != zmq::socket_type::pub) && config.socktype != zmq::socket_type::push)
            config.socktype = zmq::socket_type::pub;
        if (config.socktype == zmq::socket_type::pub)
            config.bind = true;
        return std::make_unique<HelloWorldSender>(config);
    }

    auto hello_world(std::string in) {
        static auto pub_topic = pack<HelloWorldSenderHelloWorldReceiver>(HelloWorldSenderHelloWorldReceiver::hello_world);
        auto str = pack<std::tuple<std::string>>(std::make_tuple(std::move(in)));
        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(pub_topic.data(), pub_topic.size()));
        snd_bufs.emplace_back(zmq::message_t(str.data(), str.size()));
        std::lock_guard lk(m_mtx);
        return zmq::send_multipart(m_socket, std::move(snd_bufs));
    }
    auto notice(int32_t in, std::string info) {
        static auto pub_topic = pack<HelloWorldSenderHelloWorldReceiver>(HelloWorldSenderHelloWorldReceiver::notice);
        auto str = pack<std::tuple<int32_t, std::string>>(std::make_tuple(in, std::move(info)));
        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(pub_topic.data(), pub_topic.size()));
        snd_bufs.emplace_back(zmq::message_t(str.data(), str.size()));
        std::lock_guard lk(m_mtx);
        return zmq::send_multipart(m_socket, std::move(snd_bufs));
    }

private:
    std::shared_ptr<zmq::context_t> m_context;
    zmq::socket_t m_socket;
    std::mutex m_mtx;
};

} // namespace frpc

namespace frpc {

enum class StreamClientStreamServer {
    hello_world,

};

} // namespace frpc

MSGPACK_ADD_ENUM(frpc::StreamClientStreamServer)

namespace frpc {

#ifdef __cpp_impl_coroutine

class StreamClient final {
public:
    StreamClient(const ChannelConfig& config,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_channel(std::make_unique<BiChannel>(config, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
        m_pool_ptr = std::make_unique<ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
    }

    ~StreamClient() {
        if (m_pool_ptr)
            m_pool_ptr->stop();
    }

    void start() {
        m_channel->start();
    }

    auto& socket() {
        return m_channel->socket();
    }

    auto& context() {
        return m_channel->context();
    }

    auto hello_world() {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, StreamClientStreamServer::hello_world);

        auto buffer = pack<decltype(header)>(std::move(header));
        std::string header_str((char*)buffer.data(), buffer.size());

        auto stream_ptr = std::make_shared<Stream<void(std::string)>>(
            [this, header_str = std::move(header_str)](std::string bank_name) mutable {
                auto packet = pack<std::tuple<std::string>>(std::make_tuple(std::move(bank_name)));
                std::vector<zmq::message_t> snd_bufs;
                snd_bufs.emplace_back(zmq::message_t(header_str.data(), header_str.size()));
                snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
                m_channel->send(std::move(snd_bufs));
            },
            [] {});
        auto channel_ptr = std::make_shared<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>(
            m_pool_ptr->getIoContext(),
            m_config.channel_size);
        {
            std::function<void(std::tuple<std::string>)> func = [channel_ptr, this](std::tuple<std::string> tp) mutable {
                auto& [reply] = tp;
                if (!channel_ptr->try_send(asio::error_code{}, std::move(reply)))
                    m_error(FRPC_ERROR_FORMAT("Failed to store message to channel!!!"));
            };
            std::lock_guard lk(m_mtx);
            m_cb.emplace(req_id, std::move(func));
            m_close_cb.emplace(req_id, [channel_ptr] {
                channel_ptr->close();
            });
        }
        return std::make_tuple(stream_ptr, channel_ptr);
    }

    static auto create(ChannelConfig& config, std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<StreamClient>(config, std::move(error));
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("client recv invalid stream server response packets!!!"));
            return;
        }
        try {
            auto [req_id, req_type, is_close] = unpack<std::tuple<uint64_t, StreamClientStreamServer, bool>>(recv_bufs[0].data(), recv_bufs[0].size());
            if (is_close) {
                std::unique_lock lk(m_mtx);
                m_cb.erase(req_id);
                m_close_cb[req_id]();
                m_close_cb.erase(req_id);
                return;
            }
            switch (req_type) {
                case StreamClientStreamServer::hello_world: {
                    auto [reply] = unpack<std::tuple<std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::unique_lock lk(m_mtx);
                    if (m_cb.find(req_id) == m_cb.end())
                        break;
                    auto& cb = m_cb[req_id];
                    auto callback = std::any_cast<std::function<void(std::tuple<std::string>)>>(cb);
                    lk.unlock();
                    callback(std::move(reply));
                    break;
                }
                default:
                    m_error(FRPC_ERROR_FORMAT("error type"));
            }
        } catch (const msgpack::type_error& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::bad_any_cast& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::exception& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        }
    }

    ChannelConfig m_config;
    std::unique_ptr<BiChannel> m_channel;
    std::function<void(std::string)> m_error;
    std::mutex m_mtx;
    std::unordered_map<uint64_t, std::any> m_cb;
    std::unordered_map<uint64_t, std::function<void()>> m_close_cb;
    std::atomic_uint64_t m_req_id{0};
    std::unique_ptr<ContextPool> m_pool_ptr;
};

struct StreamServerHandler {
    virtual void hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                             std::shared_ptr<Stream<void(std::string)>>) = 0;
};

struct CoroStreamServerHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                                              std::shared_ptr<Stream<void(std::string)>>) = 0;
#else
    virtual void hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                             std::shared_ptr<Stream<void(std::string)>>) = 0;
#endif
};

class StreamServer final {
public:
    StreamServer(const ChannelConfig& config,
                 std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_handler(std::move(handler))
        , m_error(error) {
        m_pool_ptr = std::make_unique<ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
        m_channel = std::make_unique<BiChannel>(config, error, [this](auto& recv_bufs) {
            dispatch(recv_bufs);
        });
    }
    ~StreamServer() {
        if (m_pool_ptr)
            m_pool_ptr->stop();
    }

    auto& socket() {
        return m_channel->socket();
    }

    auto& context() {
        return m_channel->context();
    }

    void start() {
        m_channel->start();
    }

    static auto create(ChannelConfig& config,
                       std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<StreamServer>(config, std::move(handler), std::move(error));
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 3) {
            m_error(FRPC_ERROR_FORMAT("server recv invalid stream client request packets!!!"));
            return;
        }
        try {
            auto [req_id, req_type] = unpack<std::tuple<uint64_t, StreamClientStreamServer>>(recv_bufs[1].data(), recv_bufs[1].size());
            switch (req_type) {
                case StreamClientStreamServer::hello_world: {
                    auto tp = unpack<std::tuple<std::string>>(recv_bufs[2].data(), recv_bufs[2].size());
                    auto& [bank_name] = tp;
                    std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>> channel_ptr;
                    {
                        std::lock_guard lk(m_mtx);
                        if (m_channel_mapping.contains(req_id)) {
                            channel_ptr = std::any_cast<decltype(channel_ptr)>(m_channel_mapping[req_id]);
                            if (!channel_ptr->try_send(asio::error_code{}, std::move(bank_name)))
                                m_error(FRPC_ERROR_FORMAT("Failed to store message to channel!!!"));
                            return;
                        }
                        channel_ptr = std::make_shared<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>(
                            m_pool_ptr->getIoContext(),
                            m_config.channel_size);
                        m_channel_mapping[req_id] = channel_ptr;
                    }
                    if (!channel_ptr->try_send(asio::error_code{}, std::move(bank_name)))
                        m_error(FRPC_ERROR_FORMAT("Failed to store message to channel!!!"));

                    auto is_open = std::make_tuple(req_id, req_type, false);
                    auto is_open_buffer = pack<decltype(is_open)>(is_open);
                    auto is_close = std::make_tuple(req_id, req_type, true);
                    auto is_close_buffer = pack<decltype(is_close)>(is_close);
                    // set open stream flag
                    recv_bufs[1] = zmq::message_t(is_open_buffer.data(), is_open_buffer.size());
                    // set close stream flag
                    recv_bufs[2] = zmq::message_t(is_close_buffer.data(), is_close_buffer.size());
                    auto ptr = std::make_shared<std::vector<zmq::message_t>>(std::move(recv_bufs));

                    auto out = std::make_shared<Stream<void(std::string)>>([ptr, this](std::string reply) mutable {
                        auto& recv_bufs = *ptr;
                        auto packet = pack<std::tuple<std::string>>(std::make_tuple(std::move(reply)));
                        auto close = pack<bool>(false);
                        std::vector<zmq::message_t> snd_bufs;
                        snd_bufs.emplace_back(zmq::message_t(recv_bufs[0].data(), recv_bufs[0].size()));
                        snd_bufs.emplace_back(zmq::message_t(recv_bufs[1].data(), recv_bufs[1].size()));
                        snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
                        m_channel->send(snd_bufs);
                    },
                                                                           [ptr, this, req_id, channel_ptr]() mutable {
                                                                               auto& recv_bufs = *ptr;
                                                                               std::vector<zmq::message_t> snd_bufs;
                                                                               snd_bufs.emplace_back(zmq::message_t(recv_bufs[0].data(), recv_bufs[0].size()));
                                                                               snd_bufs.emplace_back(zmq::message_t(recv_bufs[2].data(), recv_bufs[2].size()));
                                                                               snd_bufs.emplace_back(zmq::message_t("C", 1));
                                                                               m_channel->send(snd_bufs);
                                                                               channel_ptr->close();
                                                                               {
                                                                                   std::lock_guard lk(m_mtx);
                                                                                   m_channel_mapping.erase(req_id);
                                                                               }
                                                                           });
                    std::visit([&](auto&& arg) mutable {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::shared_ptr<StreamServerHandler>>) {
                            arg->hello_world(std::move(channel_ptr), std::move(out));
                        } else {
                            asio::co_spawn(
                                m_pool_ptr->getIoContext(),
                                arg->hello_world(std::move(channel_ptr), std::move(out)),
                                asio::detached);
                        }
                    },
                               m_handler);
                    break;
                }
                default:
                    m_error(FRPC_ERROR_FORMAT("error type"));
            }
        } catch (const msgpack::type_error& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::bad_any_cast& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        } catch (const std::exception& error) {
            m_error(FRPC_ERROR_FORMAT(error.what()));
        }
    }

    ChannelConfig m_config;
    std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<BiChannel> m_channel;
    std::mutex m_mtx;
    std::unique_ptr<ContextPool> m_pool_ptr;
    std::unordered_map<uint64_t, std::any> m_channel_mapping;
};

#endif

} // namespace frpc

#endif //_FRPC_H_
