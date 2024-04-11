#include <spdlog/spdlog.h>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <zmq.hpp>

#include "frpc.hpp"

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

auto create_bank_info() {
    frpc::BankInfo bank_info;
    bank_info.name = "xiaoli";
    bank_info.type = frpc::TestType::EnumOne;
    bank_info.test_one = 100;
    bank_info.test_two = 101;
    bank_info.test_map_one.emplace("frpc", frpc::TestType::EnumOne);
    bank_info.test_map.emplace(false, 555);
    bank_info.test_vector.emplace_back("vector");
    bank_info.info.name = "rpc";
    std::string bank_name = "zhongxin";
    return bank_info;
}

struct Handler : public frpc::HelloWorldServerHandler {
    virtual void hello_world(frpc::BankInfo bank_info, std::string bank_name, uint64_t blance,
                             std::function<void(std::string, frpc::Info, uint64_t)> cb) override {
        spdlog::info("frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}",
                     frpc::toString(bank_info), bank_name, blance);
        frpc::Info info;
        info.name = "test";
        cb("hello world", std::move(info), 789);
    }
};

#ifdef __cpp_impl_coroutine

struct CoroHandler : public frpc::CoroHelloWorldServerHandler {
    virtual asio::awaitable<void> hello_world(frpc::BankInfo bank_info, std::string bank_name, uint64_t blance,
                                              std::function<void(std::string, frpc::Info, uint64_t)> cb) override {
        spdlog::info("coro frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}",
                     frpc::toString(bank_info), bank_name, blance);
        frpc::Info info;
        info.name = "coro test";
        cb("coro hello world", std::move(info), 556);
        co_return;
    }
};

struct CoroHelloWorldReceiver : public frpc::CoroHelloWorldReceiverHandler {
    virtual asio::awaitable<void> hello_world(std::string in) override {
        spdlog::info("CoroHelloWorldReceiver::hello_world: {}", in);
        co_return;
    }
    virtual asio::awaitable<void> notice(int32_t in, std::string info) override {
        spdlog::info("CoroHelloWorldReceiver::notice: {}: {}", in, info);
        co_return;
    }
};

#endif

struct HelloWorldReceiverHandler : public frpc::HelloWorldReceiverHandler {
    HelloWorldReceiverHandler() = default;
    HelloWorldReceiverHandler(const std::string& label)
        : label(label) {
    }

    virtual void hello_world(std::string in) override {
        spdlog::info("HelloWorldReceiverHandler::hello_world: {}, {}", label, in);
        return;
    }
    virtual void notice(int32_t in, std::string info) override {
        spdlog::info("HelloWorldReceiverHandler::notice: {}, {}: {}", label, in, info);
        return;
    }

    std::string label{"test"};
};

auto test_push_pull() {
    start([] {
        frpc::ChannelConfig pub_config{};
        pub_config.socktype = zmq::socket_type::push;
        pub_config.bind = true;
        pub_config.addr = "tcp://127.0.0.1:5877";
        auto sender = frpc::HelloWorldSender::create(pub_config);
        int i = 10;
        while (i--) {
            sender->hello_world(std::to_string(i) + "_frpc_push_01");
            sender->notice(i, "hello world");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
    frpc::ChannelConfig sub_config{};
    sub_config.addr = "tcp://127.0.0.1:5877";
    sub_config.socktype = zmq::socket_type::pull;
    sub_config.bind = false;
    auto receiver = frpc::HelloWorldReceiver::create(
        sub_config,
        std::make_shared<HelloWorldReceiverHandler>("pull-01"),
        [](auto error) {
            spdlog::error("{}", error);
        });
    receiver->start();
    auto receiver1 = frpc::HelloWorldReceiver::create(
        sub_config,
        std::make_shared<HelloWorldReceiverHandler>("pull-02"),
        [](auto error) {
            spdlog::error("{}", error);
        });
    receiver1->start();
    std::this_thread::sleep_for(std::chrono::seconds(15));
}

auto test_pub_sub() {
    start([] {
        frpc::ChannelConfig pub_config{};
        pub_config.addr = "tcp://127.0.0.1:5877";
        pub_config.socktype = zmq::socket_type::pub;
        auto sender = frpc::HelloWorldSender::create(pub_config);
        int i = 10;
        while (i--) {
            sender->hello_world(std::to_string(i) + "_frpc");
            sender->notice(i, "hello world");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    frpc::ChannelConfig sub_config{};
    sub_config.socktype = zmq::socket_type::sub;
    sub_config.addr = "tcp://127.0.0.1:5877";
    auto receiver = frpc::HelloWorldReceiver::create(
        sub_config,
#ifdef __cpp_impl_coroutine
        std::make_shared<CoroHelloWorldReceiver>(),
#else
        std::make_shared<HelloWorldReceiverHandler>(),
#endif
        [](auto error) {
            spdlog::error("{}", error);
        });
    auto monitor = std::make_unique<frpc::Monitor>(receiver->context(), receiver->socket());
    auto event_cb = [](std::optional<std::tuple<zmq_event_t, std::string>> data) {
        if (!data.has_value())
            return;
        auto& [event, point] = data.value();
        spdlog::info("HelloWorldReceiver monitor: {} {}", frpc::getEventName(event.event), point);
    };
    monitor->start(event_cb, ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED);
    receiver->start();
    return std::make_tuple(std::move(receiver), std::move(monitor));
}

void test_bi() {
    start([] {
        frpc::ChannelConfig bi_config{};
        bi_config.addr = "tcp://127.0.0.1:5878";
        auto server = frpc::HelloWorldServer::create(
            bi_config,
            std::make_shared<Handler>(),
            [](std::string error) {
                spdlog::error("frpc::HelloWorldServer error: {}", error);
            });
        auto monitor = std::make_unique<frpc::Monitor>(server->context(), server->socket());
        auto event_cb = [](std::optional<std::tuple<zmq_event_t, std::string>> data) {
            if (!data.has_value())
                return;
            auto& [event, point] = data.value();
            spdlog::info("monitor: {} {}", frpc::getEventName(event.event), point);
        };
        monitor->start(event_cb, ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED);
        server->start();
        std::this_thread::sleep_for(std::chrono::seconds(8));
    });
    start([] {
        frpc::ChannelConfig bi_config{};
        bi_config.addr = "tcp://127.0.0.1:5878";
        auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
            spdlog::error("frpc::HelloWorldClient error: {}", error);
        });
        client->start();

        std::string bank_name = "zhongxin";
        client->hello_world(
            create_bank_info(),
            bank_name,
            999,
            [](std::string reply, frpc::Info info, uint64_t count) {
                spdlog::info("frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
            });

        client->hello_world(
            create_bank_info(),
            bank_name,
            999,
            [](std::string reply, frpc::Info info, uint64_t count) {
                spdlog::info("frpc::HelloWorldClient::hello_world(timeout) recv: {},{},{}", reply, frpc::toString(info), count);
            },
            std::chrono::milliseconds(200),
            [] {
                spdlog::info("frpc::HelloWorldClient::timeout timeout!!!");
            });

        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
}

#ifdef __cpp_impl_coroutine
void test_coro_bi(auto& pool) {
    start([] {
        frpc::ChannelConfig bi_config{};
        bi_config.addr = "tcp://127.0.0.1:5879";
        auto server = frpc::HelloWorldServer::create(
            bi_config,
            std::make_shared<CoroHandler>(),
            [](std::string error) {
                spdlog::error("frpc::HelloWorldServer error: {}", error);
            });
        server->start();
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });

    asio::co_spawn(
        pool.getIoContext(),
        [&]() -> asio::awaitable<void> {
            std::cout << std::this_thread::get_id() << std::endl;
            frpc::ChannelConfig bi_config{};
            bi_config.addr = "tcp://127.0.0.1:5879";
            auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
                spdlog::error("coro frpc::HelloWorldClient error: {}", error);
            });
            client->start();
            spdlog::info("start coro client.");

            auto [reply, info, count] = co_await client->hello_world_coro(create_bank_info(), "HF", 777, asio::as_tuple(asio::use_awaitable));
            spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);

            auto ret = co_await client->hello_world_coro(create_bank_info(), "HF", 666, std::chrono::milliseconds{500}, asio::use_awaitable);
            if (ret.has_value()) {
                auto [reply, info, count] = ret.value();
                spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
                co_return;
            }
            spdlog::error("coro frpc::HelloWorldClient::hello_world timeout");
            co_return;
        },
        asio::detached);
}
#endif

#ifdef __cpp_impl_coroutine

struct CoroStreamServerHandler : public frpc::CoroStreamServerHandler {
    virtual asio::awaitable<void> hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>> ins,
                                              std::shared_ptr<frpc::Stream<void(std::string)>> outs) {
        start([outs = std::move(outs)]() mutable {
            for (int i = 0; i < 5; i++) {
                outs->operator()(std::string("stream_server_") + std::to_string(i));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            outs->close();
            spdlog::info("stream close!!!");
        });
        for (;;) {
            auto [ec, str] = co_await ins->async_receive(asio::as_tuple(asio::use_awaitable));
            if (ec) {
                spdlog::info("stream server: {}", ec.message());
                break;
            }
            spdlog::info("stream server recv: {}", str);
        }
        co_return;
    }
};

void test_coro_stream(auto& pool) {
    start([] {
        frpc::ChannelConfig bi_config{};
        bi_config.addr = "tcp://127.0.0.1:5879";
        auto server = frpc::StreamServer::create(
            bi_config,
            std::make_shared<CoroStreamServerHandler>(),
            [](std::string error) {
                spdlog::error("frpc::StreamServer error: {}", error);
            });
        server->start();
        std::this_thread::sleep_for(std::chrono::seconds(15));
    });
    asio::co_spawn(
        pool.getIoContext(),
        [&]() -> asio::awaitable<void> {
            std::cout << std::this_thread::get_id() << std::endl;
            frpc::ChannelConfig bi_config{};
            bi_config.addr = "tcp://127.0.0.1:5879";
            auto client = frpc::StreamClient::create(bi_config, [](std::string error) {
                spdlog::error("coro frpc::StreamClient error: {}", error);
            });
            client->start();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            spdlog::info("start StreamClient client.");
            auto [tx, rx] = client->hello_world();
            start([tx]() mutable {
                for (int i = 0; i < 5; i++) {
                    tx->operator()(std::string("stream_client_") + std::to_string(i));
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                spdlog::info("send exit!!!");
            });
            for (;;) {
                auto [ec, reply] = co_await rx->async_receive(asio::as_tuple(asio::use_awaitable));
                if (ec) {
                    spdlog::info("client stream ec: {}", ec.message());
                    break;
                }
                spdlog::info("client stream read: {}", reply);
            }
            spdlog::info("recv exit!!!");
            co_return;
        },
        asio::detached);
}
#endif

int main() {
    spdlog::info("----------------test_push_pull--------------------------");
    test_push_pull();
    spdlog::info("----------------test_pub_sub----------------------------");
    auto [receiver, monitor] = test_pub_sub();
    std::this_thread::sleep_for(std::chrono::seconds(10));
    test_bi();
    std::this_thread::sleep_for(std::chrono::seconds(10));
    spdlog::info("----------------test coro--------------------------");
#ifdef __cpp_impl_coroutine
    frpc::ContextPool pool{4};
    pool.start();
    test_coro_bi(pool);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    spdlog::info("----------------test_coro_stream--------------------------");
    test_coro_stream(pool);
    std::this_thread::sleep_for(std::chrono::seconds(15));
    pool.stop();
#endif
    monitor.reset();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    return 0;
}
