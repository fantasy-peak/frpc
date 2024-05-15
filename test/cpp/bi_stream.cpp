#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "fantasy.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

#ifdef __cpp_impl_coroutine
struct CoroStreamServerHandler final : public fantasy::CoroStreamServerHandler {
    virtual asio::awaitable<void> hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(frpc::error_code, std::string)>> ins,
                                              std::shared_ptr<frpc::Stream<void(std::string)>> outs) noexcept override {
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
#else
#endif

void start_server() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto server = fantasy::StreamServer::create(
        bi_config,
        std::make_shared<CoroStreamServerHandler>(),
        [](std::string error) {
            spdlog::error("fantasy::StreamServer error: {}", error);
        });
    server->monitor(
        [](std::tuple<zmq_event_t, std::string> data) {
            auto& [event, point] = data;
            spdlog::info("fantasy::StreamServer monitor: {} {}", frpc::getEventName(event.event), point);
        },
        ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED);
    server->start();
    std::this_thread::sleep_for(std::chrono::seconds(12));
}

asio::awaitable<void> start_client() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = fantasy::StreamClient::create(bi_config, [](std::string error) {
        spdlog::error("coro fantasy::StreamClient error: {}", error);
    });
    client->monitor(
        [](std::tuple<zmq_event_t, std::string> data) {
            auto& [event, point] = data;
            spdlog::info("fantasy::StreamClient monitor: {} {}", frpc::getEventName(event.event), point);
        },
        ZMQ_EVENT_CONNECTED);
    client->start();

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
}

int main() {
    start(start_server);

    frpc::ContextPool pool{4};
    pool.start();
    asio::co_spawn(pool.getIoContext(), start_client(), asio::detached);
    std::this_thread::sleep_for(std::chrono::seconds(15));
    pool.stop();
    return 0;
}
