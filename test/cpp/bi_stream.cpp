#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "frpc.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

#ifdef __cpp_impl_coroutine
struct CoroStreamServerHandler final : public frpc::CoroStreamServerHandler {
    virtual asio::awaitable<void> hello_world(std::shared_ptr<asio::experimental::concurrent_channel<void(asio::error_code, std::string)>> ins,
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
    auto server = frpc::StreamServer::create(
        bi_config,
        std::make_shared<CoroStreamServerHandler>(),
        [](std::string error) {
            spdlog::error("frpc::StreamServer error: {}", error);
        });
    server->start();
    std::this_thread::sleep_for(std::chrono::seconds(12));
}

asio::awaitable<void> start_client() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = frpc::StreamClient::create(bi_config, [](std::string error) {
        spdlog::error("coro frpc::StreamClient error: {}", error);
    });
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
