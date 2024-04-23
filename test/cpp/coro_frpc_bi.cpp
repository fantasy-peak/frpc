#include <exception>
#include <functional>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "frpc.hpp"
#include "frpc_coroutine.h"

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

struct CoroHandler final : public frpc::FrpcCoroHelloWorldServerHandler {
    virtual frpc::Task<void> hello_world(frpc::BankInfo bank_info,
                                         std::string bank_name,
                                         uint64_t blance,
                                         std::optional<std::string> date,
                                         std::function<void(std::string, frpc::Info, uint64_t, std::optional<std::string>)> cb) noexcept override {
        spdlog::info("frpc::Task frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}, date: {}",
                     frpc::toString(bank_info), bank_name, blance, date.has_value() ? date.value() : "nullopt");
        frpc::Info info;
        info.name = "frpc::Task test";
        cb("coro hello world", std::move(info), 556, std::nullopt);
        co_return;
    }
};

frpc::Task<> start_client() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
        spdlog::error("coro frpc::HelloWorldClient error: {}", error);
    });
    client->start();
    spdlog::info("start coro client.");

    try {
        auto [reply, info, count, date] = co_await client->hello_world_coro(create_bank_info(), "HF", 996, std::nullopt);
        spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{},{}", reply, frpc::toString(info), count, date.has_value() ? date.value() : "nullopt");

        // auto ret = co_await client->hello_world_coro(create_bank_info(), "HF", 666, std::nullopt, std::chrono::milliseconds{500}, asio::use_awaitable);
        // if (ret.has_value()) {
        //     auto [reply, info, count, date] = ret.value();
        //     spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{},{}", reply, frpc::toString(info), count, date.has_value() ? date.value() : "nullopt");
        //     co_return;
        // }
        // spdlog::error("coro frpc::HelloWorldClient::hello_world timeout");
    } catch (std::exception e) {
        std::cout << "e:" << e.what() << std::endl;
    }
    std::cout << "aaaaaaaaaaaaaaa" << std::endl;
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
    server->monitor(
        [](std::tuple<zmq_event_t, std::string> data) {
            auto& [event, point] = data;
            spdlog::info("bi coro server monitor: {} {}", frpc::getEventName(event.event), point);
        },
        ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED);
    server->start();
    std::this_thread::sleep_for(std::chrono::seconds(10));
}

int main() {
    start(start_server);

    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
        spdlog::error("coro frpc::HelloWorldClient error: {}", error);
    });
    client->start();

    frpc::async_run([&]() -> frpc::Task<> {
        spdlog::info("start coro client.");
        auto [reply, info, count, date] = co_await client->hello_world_coro(create_bank_info(), "HF", 996, std::nullopt);
        spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{},{}", reply, frpc::toString(info), count, date.has_value() ? date.value() : "nullopt");

        auto ret = co_await client->hello_world_coro(create_bank_info(), "HF", 666, std::nullopt, std::chrono::milliseconds{500});
        if (ret.has_value()) {
            auto [reply, info, count, date] = ret.value();
            spdlog::info("coro frpc::HelloWorldClient::hello_world recv: {},{},{},{}", reply, frpc::toString(info), count, date.has_value() ? date.value() : "nullopt");
            co_return;
        }
        spdlog::error("coro frpc::HelloWorldClient::hello_world timeout");
        co_return;
    });

    std::this_thread::sleep_for(std::chrono::seconds(15));
}
