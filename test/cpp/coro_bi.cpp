#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "frpc.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

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

struct CoroHandler final : public frpc::CoroHelloWorldServerHandler {
    virtual asio::awaitable<void> hello_world(frpc::BankInfo bank_info,
                                              std::string bank_name,
                                              uint64_t blance,
                                              std::function<void(std::string, frpc::Info, uint64_t)> cb) override {
        spdlog::info("coro frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}",
                     frpc::toString(bank_info), bank_name, blance);
        frpc::Info info;
        info.name = "coro test";
        cb("coro hello world", std::move(info), 556);
        co_return;
    }
};

asio::awaitable<void> start_client() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
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
}

void start_server() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto server = frpc::HelloWorldServer::create(
        bi_config,
        std::make_shared<CoroHandler>(),
        [](std::string error) {
            spdlog::error("coro frpc::HelloWorldServer error: {}", error);
        });
    server->start();
    std::this_thread::sleep_for(std::chrono::seconds(5));
}

int main() {
    start(start_server);

    frpc::ContextPool pool{4};
    pool.start();
    asio::co_spawn(pool.getIoContext(), start_client(), asio::detached);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    pool.stop();

    return 0;
}
