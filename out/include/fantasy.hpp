#ifndef _FANTASY_H_
#define _FANTASY_H_

#include <any>
#include <mutex>
#include <optional>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
#include <variant>

#include <nlohmann/json.hpp>

#define FRPC_ERROR_FORMAT(message) [](const std::string& info) { \
    std::stringstream ss;                                        \
    ss << __FILE__ << ":" << __LINE__ << " " << info;            \
    return ss.str();                                             \
}(message)

#include <impl/bi_channel.h>
#include <impl/monitor.h>
#include <impl/to_string.h>
#include <impl/uni_channel.h>
#include <impl/utils.h>
#ifdef __cpp_impl_coroutine
#include <impl/asio_context_pool.h>
#include <impl/coroutine.h>
#endif

#include <data/test_type.h>

// clang-format off
#include <data/info.h>
#include <data/bank_info.h>
// clang-format on

namespace fantasy {

enum class HelloWorldClientHelloWorldServer : uint16_t {
    hello_world = 1,
};

} // namespace fantasy

MSGPACK_ADD_ENUM(fantasy::HelloWorldClientHelloWorldServer)

namespace fantasy {

class HelloWorldClient final {
public:
    HelloWorldClient(const frpc::ChannelConfig& config,
                     std::function<void(std::string)> error)
        : m_channel(std::make_unique<frpc::BiChannel>(config, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
    }
    HelloWorldClient(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context_ptr,
                     const std::shared_ptr<zmq::socket_t>& socket_ptr,
                     std::function<void(std::string)> error)
        : m_channel(std::make_unique<frpc::BiChannel>(config, context_ptr, socket_ptr, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
    }
    HelloWorldClient(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context_ptr,
                     std::function<void(std::string)> error)
        : m_channel(std::make_unique<frpc::BiChannel>(config, context_ptr, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
    }

    void start() {
        m_channel->start();
    }

    decltype(auto) socket() {
        return m_channel->socket();
    }

    decltype(auto) context() {
        return m_channel->context();
    }

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb, int events = ZMQ_EVENT_ALL) {
        return m_channel->monitor(std::move(cb), events);
    }

    void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, HelloWorldClientHelloWorldServer::hello_world);
        auto buffer = frpc::pack<decltype(header)>(header);
        auto packet = frpc::pack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(bank_info), std::move(bank_name), blance, std::move(date)));

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
                     const std::chrono::milliseconds& timeout,
                     std::function<void()> timeout_cb) {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, HelloWorldClientHelloWorldServer::hello_world);
        auto buffer = frpc::pack<decltype(header)>(header);
        auto packet = frpc::pack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(bank_info), std::move(bank_name), blance, std::move(date)));

        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(buffer.data(), buffer.size()));
        snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
        {
            std::lock_guard lk(m_mtx);
            m_cb.emplace(req_id, std::move(cb));
            m_timeout_cb.emplace(req_id, std::move(timeout_cb));
        }
        m_channel->send(std::move(snd_bufs), timeout, [this, req_id] {
            callTimeoutCallback(req_id);
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
    auto hello_world_coro(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, const std::chrono::milliseconds& timeout, CompletionToken&& token) {
        return asio::async_initiate<CompletionToken, void(std::optional<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>)>(
            [this]<typename Handler>(Handler&& handler, BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, const auto& timeout) mutable {
                auto handler_ptr = std::make_shared<Handler>(std::move(handler));
                this->hello_world(
                    std::move(bank_info), std::move(bank_name), blance, std::move(date),
                    [handler_ptr](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                        auto ex = asio::get_associated_executor(*handler_ptr);
                        asio::post(ex, [reply = std::move(reply), info = std::move(info), count, date = std::move(date), handler_ptr = std::move(handler_ptr)]() mutable -> void {
                            (*handler_ptr)(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                        });
                    },
                    timeout,
                    [handler_ptr]() mutable {
                        auto ex = asio::get_associated_executor(*handler_ptr);
                        asio::post(ex, [=, handler_ptr = std::move(handler_ptr)]() mutable -> void {
                            (*handler_ptr)(std::nullopt);
                        });
                    });
            },
            token,
            std::move(bank_info), std::move(bank_name), blance, std::move(date), timeout);
    }

    auto hello_world_coro(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date) {
        return frpc::CallbackAwaiter<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>{
            [this, bank_info = std::move(bank_info), bank_name = std::move(bank_name), blance, date = std::move(date)](std::coroutine_handle<> handle, auto set_resume_value) mutable {
                this->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date),
                                  [handle, set_resume_value = std::move(set_resume_value)](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                                      set_resume_value(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                                      handle.resume();
                                  });
            }};
    }

    auto hello_world_coro(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, const std::chrono::milliseconds& timeout) {
        return frpc::CallbackAwaiter<std::optional<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>>{
            [this, bank_info = std::move(bank_info), bank_name = std::move(bank_name), blance, date = std::move(date), &timeout](std::coroutine_handle<> handle, auto set_resume_value) mutable {
                this->hello_world(
                    std::move(bank_info), std::move(bank_name), blance, std::move(date),
                    [handle, set_resume_value](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                        set_resume_value(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                        handle.resume();
                    },
                    timeout,
                    [handle, set_resume_value]() {
                        set_resume_value(std::nullopt);
                        handle.resume();
                    });
            }};
    }

#endif

    static auto create(frpc::ChannelConfig& config, std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<HelloWorldClient>(config, std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       std::function<void(std::string)> error) {
        return std::make_unique<HelloWorldClient>(config, context_ptr, socket_ptr, std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<HelloWorldClient>(config, context_ptr, std::move(error));
    }

private:
    void callTimeoutCallback(uint64_t req_id) {
        std::unique_lock lk(m_mtx);
        if (m_timeout_cb.find(req_id) == m_timeout_cb.end())
            return;
        auto cb = std::move(m_timeout_cb[req_id]);
        m_timeout_cb.erase(req_id);
        m_cb.erase(req_id);
        lk.unlock();
        cb();
    }

    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("Illegal response packet"));
            return;
        }
        try {
            auto [req_id, req_type] = frpc::unpack<std::tuple<uint64_t, HelloWorldClientHelloWorldServer>>(recv_bufs[0].data(), recv_bufs[0].size());
            std::unique_lock lk(m_mtx);
            if (m_cb.find(req_id) == m_cb.end())
                return;
            auto cb = std::move(m_cb[req_id]);
            m_cb.erase(req_id);
            m_timeout_cb.erase(req_id);
            lk.unlock();
            switch (req_type) {
                case HelloWorldClientHelloWorldServer::hello_world: {
                    auto [reply, info, count, date] = frpc::unpack<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>(recv_bufs[1].data(), recv_bufs[1].size());
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

    std::unique_ptr<frpc::BiChannel> m_channel;
    std::function<void(std::string)> m_error;
    std::mutex m_mtx;
    std::unordered_map<uint64_t, std::any> m_cb;
    std::unordered_map<uint64_t, std::function<void()>> m_timeout_cb;
    std::atomic_uint64_t m_req_id{0};
};

struct HelloWorldServerHandler {
    virtual void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) noexcept = 0;
};

struct AsioCoroHelloWorldServerHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) noexcept = 0;
#else
    virtual void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) noexcept = 0;
#endif
};

struct FrpcCoroHelloWorldServerHandler {
#ifdef __cpp_impl_coroutine
    virtual frpc::Task<void> hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) noexcept = 0;
#else
    virtual void hello_world(BankInfo bank_info, std::string bank_name, uint64_t blance, std::optional<std::string> date, std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> cb) noexcept = 0;
#endif
};

class HelloWorldServer final {
public:
    using VariantHandler = std::variant<std::shared_ptr<FrpcCoroHelloWorldServerHandler>, std::shared_ptr<AsioCoroHelloWorldServerHandler>, std::shared_ptr<HelloWorldServerHandler>>;

    HelloWorldServer(const frpc::ChannelConfig& config,
                     VariantHandler handler,
                     std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::BiChannel>(config, error, [this](std::vector<zmq::message_t>& recv_bufs) mutable {
            dispatch(recv_bufs);
        });
    }
    HelloWorldServer(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context_ptr,
                     const std::shared_ptr<zmq::socket_t>& socket_ptr,
                     VariantHandler handler,
                     std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::BiChannel>(config, context_ptr, socket_ptr, error, [this](std::vector<zmq::message_t>& recv_bufs) mutable {
            dispatch(recv_bufs);
        });
    }
    HelloWorldServer(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context_ptr,
                     VariantHandler handler,
                     std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::BiChannel>(config, context_ptr, error, [this](std::vector<zmq::message_t>& recv_bufs) mutable {
            dispatch(recv_bufs);
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

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb, int events = ZMQ_EVENT_ALL) {
        return m_channel->monitor(std::move(cb), events);
    }

    void start() {
        m_channel->start();
    }

    static auto create(frpc::ChannelConfig& config,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<HelloWorldServer>(config, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        config.bind = true;
        return std::make_unique<HelloWorldServer>(config, context_ptr, socket_ptr, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<HelloWorldServer>(config, context_ptr, std::move(handler), std::move(error));
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 3) {
            m_error(FRPC_ERROR_FORMAT("BiChannel recv illegal request packet"));
            return;
        }
        try {
            auto [req_id, req_type] = frpc::unpack<std::tuple<uint64_t, HelloWorldClientHelloWorldServer>>(recv_bufs[1].data(), recv_bufs[1].size());
            switch (req_type) {
                case HelloWorldClientHelloWorldServer::hello_world: {
                    auto tp = frpc::unpack<std::tuple<BankInfo, std::string, uint64_t, std::optional<std::string>>>(recv_bufs[2].data(), recv_bufs[2].size());
                    auto recv_bufs_ptr = std::make_shared<std::vector<zmq::message_t>>(std::move(recv_bufs));
                    // Don't call it in multiple threads
                    std::function<void(std::string, Info, uint64_t, std::optional<std::string>)> out = [done = false, recv_bufs_ptr = std::move(recv_bufs_ptr), this](std::string reply, Info info, uint64_t count, std::optional<std::string> date) mutable {
                        if (done)
                            return;
                        done = true;
                        auto& snd_bufs = *recv_bufs_ptr;
                        auto packet = frpc::pack<std::tuple<std::string, Info, uint64_t, std::optional<std::string>>>(std::make_tuple(std::move(reply), std::move(info), count, std::move(date)));
                        snd_bufs[2] = zmq::message_t(packet.data(), packet.size());
                        m_channel->send(std::move(snd_bufs));
                    };
                    std::visit([&](auto&& arg) mutable {
                        auto& [bank_info, bank_name, blance, date] = tp;
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldServerHandler>>) {
                            arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out));
                        } else if constexpr (std::is_same_v<T, std::shared_ptr<AsioCoroHelloWorldServerHandler>>) {
#ifdef __cpp_impl_coroutine
                            asio::co_spawn(
                                m_pool_ptr->getIoContext(),
                                arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out)),
                                asio::detached);
#else
                            arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out));
#endif
                        } else {
#ifdef __cpp_impl_coroutine
                            [](auto& arg, auto tp, auto out) mutable -> frpc::AsyncTask {
                                auto& [bank_info, bank_name, blance, date] = tp;
                                co_await arg->hello_world(std::move(bank_info), std::move(bank_name), blance, std::move(date), std::move(out));
                            }(arg, std::move(tp), std::move(out));
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
    }

    std::variant<std::shared_ptr<FrpcCoroHelloWorldServerHandler>, std::shared_ptr<AsioCoroHelloWorldServerHandler>, std::shared_ptr<HelloWorldServerHandler>> m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<frpc::BiChannel> m_channel;
    std::mutex m_mtx;
#ifdef __cpp_impl_coroutine
    std::unique_ptr<frpc::ContextPool> m_pool_ptr;
#endif
};

} // namespace fantasy

namespace fantasy {

enum class HelloWorldSenderHelloWorldReceiver : uint16_t {
    hello_world = 1,
    notice = 2,
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

} // namespace fantasy

MSGPACK_ADD_ENUM(fantasy::HelloWorldSenderHelloWorldReceiver)

namespace fantasy {

struct HelloWorldReceiverHandler {
    virtual void hello_world(std::string in) noexcept = 0;
    virtual void notice(int32_t in, std::string info) noexcept = 0;
};

struct AsioCoroHelloWorldReceiverHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(std::string in) noexcept = 0;
    virtual asio::awaitable<void> notice(int32_t in, std::string info) noexcept = 0;
#else
    virtual void hello_world(std::string in) noexcept = 0;
    virtual void notice(int32_t in, std::string info) noexcept = 0;
#endif
};

struct FrpcCoroHelloWorldReceiverHandler {
#ifdef __cpp_impl_coroutine
    virtual frpc::Task<void> hello_world(std::string in) noexcept = 0;
    virtual frpc::Task<void> notice(int32_t in, std::string info) noexcept = 0;
#else
    virtual void hello_world(std::string in) noexcept = 0;
    virtual void notice(int32_t in, std::string info) noexcept = 0;
#endif
};

class HelloWorldReceiver final {
public:
    using VariantHandler = std::variant<std::shared_ptr<FrpcCoroHelloWorldReceiverHandler>, std::shared_ptr<AsioCoroHelloWorldReceiverHandler>, std::shared_ptr<HelloWorldReceiverHandler>>;
    HelloWorldReceiver(const frpc::ChannelConfig& config,
                       VariantHandler handler,
                       std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::UniChannel>(config, [this](auto& recv) mutable {
            dispatch(recv);
        },
                                                       error);
    }
    HelloWorldReceiver(const frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::UniChannel>(config, context_ptr, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        },
                                                       error);
    }
    HelloWorldReceiver(const frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error)
        : m_handler(std::move(handler))
        , m_error(error) {
#ifdef __cpp_impl_coroutine
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
#endif
        m_channel = std::make_unique<frpc::UniChannel>(config, context_ptr, socket_ptr, [this](auto& recv_msgs) mutable {
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

    decltype(auto) socket() {
        return m_channel->socket();
    }

    decltype(auto) context() {
        return m_channel->context();
    }

    static auto create(frpc::ChannelConfig& config,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        if ((config.socktype != zmq::socket_type::sub) && config.socktype != zmq::socket_type::pull)
            config.socktype = zmq::socket_type::sub;
        if (config.socktype == zmq::socket_type::sub)
            config.bind = false;
        return std::make_unique<HelloWorldReceiver>(config, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        if ((config.socktype != zmq::socket_type::sub) && config.socktype != zmq::socket_type::pull)
            config.socktype = zmq::socket_type::sub;
        if (config.socktype == zmq::socket_type::sub)
            config.bind = false;
        return std::make_unique<HelloWorldReceiver>(config, context_ptr, socket_ptr, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       VariantHandler handler,
                       std::function<void(std::string)> error) {
        if ((config.socktype != zmq::socket_type::sub) && config.socktype != zmq::socket_type::pull)
            config.socktype = zmq::socket_type::sub;
        if (config.socktype == zmq::socket_type::sub)
            config.bind = false;
        return std::make_unique<HelloWorldReceiver>(config, context_ptr, std::move(handler), std::move(error));
    }

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb, int events = ZMQ_EVENT_ALL) {
        return m_channel->monitor(std::move(cb), events);
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("Illegal response packet"));
            return;
        }
        try {
            auto req_type = frpc::unpack<HelloWorldSenderHelloWorldReceiver>(recv_bufs[0].data(), recv_bufs[0].size());
            switch (req_type) {
                case HelloWorldSenderHelloWorldReceiver::hello_world: {
                    auto tp = frpc::unpack<std::tuple<std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::visit(
                        [&](auto&& arg) {
                            auto& [in] = tp;
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldReceiverHandler>>) {
                                arg->hello_world(std::move(in));
                            } else if constexpr (std::is_same_v<T, std::shared_ptr<AsioCoroHelloWorldReceiverHandler>>) {
#ifdef __cpp_impl_coroutine
                                asio::co_spawn(
                                    m_pool_ptr->getIoContext(),
                                    arg->hello_world(std::move(in)),
                                    asio::detached);
#else
                                arg->hello_world(std::move(in));
#endif
                            } else {
#ifdef __cpp_impl_coroutine
                                [](auto& arg, auto tp) mutable -> frpc::AsyncTask {
                                    auto& [in] = tp;
                                    co_await arg->hello_world(std::move(in));
                                }(arg, std::move(tp));
#else
                                arg->hello_world(std::move(in));
#endif
                            }
                        },
                        m_handler);
                    break;
                }
                case HelloWorldSenderHelloWorldReceiver::notice: {
                    auto tp = frpc::unpack<std::tuple<int32_t, std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
                    std::visit(
                        [&](auto&& arg) {
                            auto& [in, info] = tp;
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, std::shared_ptr<HelloWorldReceiverHandler>>) {
                                arg->notice(in, std::move(info));
                            } else if constexpr (std::is_same_v<T, std::shared_ptr<AsioCoroHelloWorldReceiverHandler>>) {
#ifdef __cpp_impl_coroutine
                                asio::co_spawn(
                                    m_pool_ptr->getIoContext(),
                                    arg->notice(in, std::move(info)),
                                    asio::detached);
#else
                                arg->notice(in, std::move(info));
#endif
                            } else {
#ifdef __cpp_impl_coroutine
                                [](auto& arg, auto tp) mutable -> frpc::AsyncTask {
                                    auto& [in, info] = tp;
                                    co_await arg->notice(in, std::move(info));
                                }(arg, std::move(tp));
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

    VariantHandler m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<frpc::UniChannel> m_channel;
#ifdef __cpp_impl_coroutine
    std::unique_ptr<frpc::ContextPool> m_pool_ptr;
#endif
};

class HelloWorldSender final {
public:
    HelloWorldSender(const frpc::ChannelConfig& config)
        : m_context(std::make_shared<zmq::context_t>(config.io_threads))
        , m_socket(std::make_shared<zmq::socket_t>(*m_context, config.socktype)) {
        init_socket(config);
    }
    HelloWorldSender(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context)
        : m_context(context)
        , m_socket(std::make_shared<zmq::socket_t>(*m_context, config.socktype)) {
        init_socket(config);
    }
    HelloWorldSender(const frpc::ChannelConfig& config,
                     const std::shared_ptr<zmq::context_t>& context,
                     const std::shared_ptr<zmq::socket_t>& socket)
        : m_context(context)
        , m_socket(socket) {
        init_socket(config);
    }

    ~HelloWorldSender() {
    }

    static auto create(frpc::ChannelConfig& config) {
        if ((config.socktype != zmq::socket_type::pub) && config.socktype != zmq::socket_type::push)
            config.socktype = zmq::socket_type::pub;
        if (config.socktype == zmq::socket_type::pub)
            config.bind = true;
        return std::make_unique<HelloWorldSender>(config);
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr) {
        if ((config.socktype != zmq::socket_type::pub) && config.socktype != zmq::socket_type::push)
            config.socktype = zmq::socket_type::pub;
        if (config.socktype == zmq::socket_type::pub)
            config.bind = true;
        return std::make_unique<HelloWorldSender>(config, context_ptr, socket_ptr);
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr) {
        if ((config.socktype != zmq::socket_type::pub) && config.socktype != zmq::socket_type::push)
            config.socktype = zmq::socket_type::pub;
        if (config.socktype == zmq::socket_type::pub)
            config.bind = true;
        return std::make_unique<HelloWorldSender>(config, context_ptr);
    }

    auto hello_world(std::string in) {
        static auto pub_topic = frpc::pack<HelloWorldSenderHelloWorldReceiver>(HelloWorldSenderHelloWorldReceiver::hello_world);
        auto str = frpc::pack<std::tuple<std::string>>(std::make_tuple(std::move(in)));
        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(pub_topic.data(), pub_topic.size()));
        snd_bufs.emplace_back(zmq::message_t(str.data(), str.size()));
        std::lock_guard lk(m_mtx);
        auto ret = zmq::send_multipart(*m_socket, std::move(snd_bufs));
        return ret;
    }
    auto notice(int32_t in, std::string info) {
        static auto pub_topic = frpc::pack<HelloWorldSenderHelloWorldReceiver>(HelloWorldSenderHelloWorldReceiver::notice);
        auto str = frpc::pack<std::tuple<int32_t, std::string>>(std::make_tuple(in, std::move(info)));
        std::vector<zmq::message_t> snd_bufs;
        snd_bufs.emplace_back(zmq::message_t(pub_topic.data(), pub_topic.size()));
        snd_bufs.emplace_back(zmq::message_t(str.data(), str.size()));
        std::lock_guard lk(m_mtx);
        auto ret = zmq::send_multipart(*m_socket, std::move(snd_bufs));
        return ret;
    }

    auto& socket() {
        return m_socket;
    }

    auto& context() {
        return m_context;
    }

private:
    void init_socket(const frpc::ChannelConfig& config) {
        m_socket->set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket->set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket->set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket->set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket->set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket->set(zmq::sockopt::tcp_keepalive, 1);
            m_socket->set(zmq::sockopt::tcp_keepalive_idle, config.tcp_keepalive_idle);
            m_socket->set(zmq::sockopt::tcp_keepalive_cnt, config.tcp_keepalive_cnt);
            m_socket->set(zmq::sockopt::tcp_keepalive_intvl, config.tcp_keepalive_intvl);
        }
        if (config.bind)
            m_socket->bind(config.addr);
        else
            m_socket->connect(config.addr);
    }

    std::shared_ptr<zmq::context_t> m_context;
    std::shared_ptr<zmq::socket_t> m_socket;
    std::mutex m_mtx;
};

} // namespace fantasy

namespace fantasy {

enum class StreamClientStreamServer : uint16_t {
    hello_world = 1,
};

} // namespace fantasy

MSGPACK_ADD_ENUM(fantasy::StreamClientStreamServer)

namespace fantasy {

#ifdef __cpp_impl_coroutine

class StreamClient final {
public:
    StreamClient(const frpc::ChannelConfig& config,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_channel(std::make_unique<frpc::BiChannel>(config, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
    }
    StreamClient(const frpc::ChannelConfig& config,
                 const std::shared_ptr<zmq::context_t>& context_ptr,
                 const std::shared_ptr<zmq::socket_t>& socket_ptr,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_channel(std::make_unique<frpc::BiChannel>(config, context_ptr, socket_ptr, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
    }
    StreamClient(const frpc::ChannelConfig& config,
                 const std::shared_ptr<zmq::context_t>& context_ptr,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_channel(std::make_unique<frpc::BiChannel>(config, context_ptr, error, [this](auto& recv_msgs) mutable {
            dispatch(recv_msgs);
        }))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
    }
    ~StreamClient() {
        if (m_pool_ptr)
            m_pool_ptr->stop();
    }

    void start() {
        m_channel->start();
    }

    decltype(auto) socket() {
        return m_channel->socket();
    }

    decltype(auto) context() {
        return m_channel->context();
    }

    auto hello_world() {
        auto req_id = m_req_id.fetch_add(1);
        auto header = std::make_tuple(req_id, StreamClientStreamServer::hello_world);

        auto buffer = frpc::pack<decltype(header)>(header);
        std::string header_str((char*)buffer.data(), buffer.size());

        auto stream_ptr = std::make_shared<frpc::Stream<void(std::string)>>(
            [this, header_str = std::move(header_str)](std::string bank_name) mutable {
                auto packet = frpc::pack<std::tuple<std::string>>(std::make_tuple(std::move(bank_name)));
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

    static auto create(frpc::ChannelConfig& config, std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<StreamClient>(config, std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       std::function<void(std::string)> error) {
        return std::make_unique<StreamClient>(config, context_ptr, socket_ptr, std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::dealer;
        return std::make_unique<StreamClient>(config, context_ptr, std::move(error));
    }

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb, int events = ZMQ_EVENT_ALL) {
        return m_channel->monitor(std::move(cb), events);
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 2) {
            m_error(FRPC_ERROR_FORMAT("client recv invalid stream server response packets!!!"));
            return;
        }
        try {
            auto [req_id, req_type, is_close] = frpc::unpack<std::tuple<uint64_t, StreamClientStreamServer, bool>>(recv_bufs[0].data(), recv_bufs[0].size());
            if (is_close) {
                std::unique_lock lk(m_mtx);
                m_cb.erase(req_id);
                m_close_cb[req_id]();
                m_close_cb.erase(req_id);
                return;
            }
            switch (req_type) {
                case StreamClientStreamServer::hello_world: {
                    auto [reply] = frpc::unpack<std::tuple<std::string>>(recv_bufs[1].data(), recv_bufs[1].size());
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

    frpc::ChannelConfig m_config;
    std::unique_ptr<frpc::BiChannel> m_channel;
    std::function<void(std::string)> m_error;
    std::mutex m_mtx;
    std::unordered_map<uint64_t, std::any> m_cb;
    std::unordered_map<uint64_t, std::function<void()>> m_close_cb;
    std::atomic_uint64_t m_req_id{0};
    std::unique_ptr<frpc::ContextPool> m_pool_ptr;
};

struct StreamServerHandler {
    virtual void hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                             std::shared_ptr<frpc::Stream<void(std::string)>>) noexcept = 0;
};

struct CoroStreamServerHandler {
#ifdef __cpp_impl_coroutine
    virtual asio::awaitable<void> hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                                              std::shared_ptr<frpc::Stream<void(std::string)>>) noexcept = 0;
#else
    virtual void hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>>,
                             std::shared_ptr<frpc::Stream<void(std::string)>>) noexcept = 0;
#endif
};

class StreamServer final {
public:
    StreamServer(const frpc::ChannelConfig& config,
                 std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_handler(std::move(handler))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
        m_channel = std::make_unique<frpc::BiChannel>(config, error, [this](auto& recv_bufs) {
            dispatch(recv_bufs);
        });
    }
    StreamServer(const frpc::ChannelConfig& config,
                 const std::shared_ptr<zmq::context_t>& context_ptr,
                 const std::shared_ptr<zmq::socket_t>& socket_ptr,
                 std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_handler(std::move(handler))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
        m_channel = std::make_unique<frpc::BiChannel>(config, context_ptr, socket_ptr, error, [this](auto& recv_bufs) {
            dispatch(recv_bufs);
        });
    }
    StreamServer(const frpc::ChannelConfig& config,
                 const std::shared_ptr<zmq::context_t>& context_ptr,
                 std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                 std::function<void(std::string)> error)
        : m_config(config)
        , m_handler(std::move(handler))
        , m_error(error) {
        m_pool_ptr = std::make_unique<frpc::ContextPool>(config.context_pool_size);
        m_pool_ptr->start();
        m_channel = std::make_unique<frpc::BiChannel>(config, context_ptr, error, [this](auto& recv_bufs) {
            dispatch(recv_bufs);
        });
    }
    ~StreamServer() {
        if (m_pool_ptr)
            m_pool_ptr->stop();
    }

    decltype(auto) socket() {
        return m_channel->socket();
    }

    decltype(auto) context() {
        return m_channel->context();
    }

    void start() {
        m_channel->start();
    }

    static auto create(frpc::ChannelConfig& config,
                       std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<StreamServer>(config, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       const std::shared_ptr<zmq::socket_t>& socket_ptr,
                       std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<StreamServer>(config, context_ptr, socket_ptr, std::move(handler), std::move(error));
    }
    static auto create(frpc::ChannelConfig& config,
                       const std::shared_ptr<zmq::context_t>& context_ptr,
                       std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> handler,
                       std::function<void(std::string)> error) {
        config.socktype = zmq::socket_type::router;
        config.bind = true;
        return std::make_unique<StreamServer>(config, context_ptr, std::move(handler), std::move(error));
    }

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb, int events = ZMQ_EVENT_ALL) {
        return m_channel->monitor(std::move(cb), events);
    }

private:
    void dispatch(std::vector<zmq::message_t>& recv_bufs) {
        if (recv_bufs.size() != 3) {
            m_error(FRPC_ERROR_FORMAT("server recv invalid stream client request packets!!!"));
            return;
        }
        try {
            auto [req_id, req_type] = frpc::unpack<std::tuple<uint64_t, StreamClientStreamServer>>(recv_bufs[1].data(), recv_bufs[1].size());
            switch (req_type) {
                case StreamClientStreamServer::hello_world: {
                    auto tp = frpc::unpack<std::tuple<std::string>>(recv_bufs[2].data(), recv_bufs[2].size());
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
                    auto is_open_buffer = frpc::pack<decltype(is_open)>(is_open);
                    auto is_close = std::make_tuple(req_id, req_type, true);
                    auto is_close_buffer = frpc::pack<decltype(is_close)>(is_close);
                    // set open stream flag
                    recv_bufs[1] = zmq::message_t(is_open_buffer.data(), is_open_buffer.size());
                    // set close stream flag
                    recv_bufs[2] = zmq::message_t(is_close_buffer.data(), is_close_buffer.size());
                    auto ptr = std::make_shared<std::vector<zmq::message_t>>(std::move(recv_bufs));

                    auto out = std::make_shared<frpc::Stream<void(std::string)>>(
                        [ptr, this](std::string reply) mutable {
                            auto& recv_bufs = *ptr;
                            auto packet = frpc::pack<std::tuple<std::string>>(std::make_tuple(std::move(reply)));
                            auto close = frpc::pack<bool>(false);
                            std::vector<zmq::message_t> snd_bufs;
                            snd_bufs.emplace_back(zmq::message_t(recv_bufs[0].data(), recv_bufs[0].size()));
                            snd_bufs.emplace_back(zmq::message_t(recv_bufs[1].data(), recv_bufs[1].size()));
                            snd_bufs.emplace_back(zmq::message_t(packet.data(), packet.size()));
                            m_channel->send(std::move(snd_bufs));
                        },
                        [ptr, this, req_id, channel_ptr]() mutable {
                            auto& recv_bufs = *ptr;
                            std::vector<zmq::message_t> snd_bufs;
                            snd_bufs.emplace_back(zmq::message_t(recv_bufs[0].data(), recv_bufs[0].size()));
                            snd_bufs.emplace_back(zmq::message_t(recv_bufs[2].data(), recv_bufs[2].size()));
                            snd_bufs.emplace_back(zmq::message_t("C", 1));
                            m_channel->send(std::move(snd_bufs));
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

    frpc::ChannelConfig m_config;
    std::variant<std::shared_ptr<CoroStreamServerHandler>, std::shared_ptr<StreamServerHandler>> m_handler;
    std::function<void(std::string)> m_error;
    std::unique_ptr<frpc::BiChannel> m_channel;
    std::mutex m_mtx;
    std::unique_ptr<frpc::ContextPool> m_pool_ptr;
    std::unordered_map<uint64_t, std::any> m_channel_mapping;
};

#endif

} // namespace fantasy

#endif //_FANTASY_H_
