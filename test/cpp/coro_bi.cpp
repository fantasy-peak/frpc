#include <functional>
#include <optional>
#include <string>

#include <spdlog/spdlog.h>

#include "fantasy.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

auto create_bank_info() {
    fantasy::BankInfo bank_info;
    bank_info.name = "xiaoli";
    bank_info.type = fantasy::TestType::EnumOne;
    bank_info.test_one = 100;
    bank_info.test_two = 101;
    bank_info.test_map_one.emplace("frpc", fantasy::TestType::EnumOne);
    bank_info.test_map.emplace(false, 555);
    bank_info.test_vector.emplace_back("vector");
    bank_info.info.name = std::nullopt;
    bank_info.date_time = frpc::getFrpcDateTime();
    return bank_info;
}

struct CoroHandler final : public fantasy::AsioCoroHelloWorldServerHandler {
    virtual asio::awaitable<void> hello_world(fantasy::BankInfo bank_info,
                                              std::string bank_name,
                                              uint64_t blance,
                                              std::optional<std::string> date,
                                              frpc::DateTime date_time,
                                              std::function<void(std::string, fantasy::Info, uint64_t, std::optional<std::string>)> cb) noexcept override {
        spdlog::info("coro fantasy::HelloWorldServer server recv: {}, bank_name: {}, blance: {}, date: {}, date_time: {}",
                     fantasy::toString(bank_info), bank_name, blance, date.has_value() ? date.value() : "nullopt", frpc::toString(date_time));
        fantasy::Info info;
        info.name = "coro test";
        cb("coro hello world", std::move(info), 556, std::nullopt);
        co_return;
    }
};

asio::awaitable<void> start_client() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = fantasy::HelloWorldClient::create(bi_config, [](std::string error) {
        spdlog::error("coro fantasy::HelloWorldClient error: {}", error);
    });
    client->start();
    spdlog::info("start coro client.");

    auto [reply, info, count, date] = co_await client->hello_world_coro(create_bank_info(), "HF", 777, std::nullopt, frpc::getFrpcDateTime(), asio::as_tuple(asio::use_awaitable));
    spdlog::info("coro fantasy::HelloWorldClient::hello_world recv: {},{},{},{}", reply, fantasy::toString(info), count, date.has_value() ? date.value() : "nullopt");

    auto ret = co_await client->hello_world_coro(create_bank_info(), "HF", 666, std::nullopt, frpc::getFrpcDateTime(), std::chrono::milliseconds{500}, asio::use_awaitable);
    if (ret.has_value()) {
        auto [reply, info, count, date] = ret.value();
        spdlog::info("coro fantasy::HelloWorldClient::hello_world recv: {},{},{},{}", reply, fantasy::toString(info), count, date.has_value() ? date.value() : "nullopt");
        co_return;
    }
    spdlog::error("coro fantasy::HelloWorldClient::hello_world timeout");
    co_return;
}

void start_server() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto server = fantasy::HelloWorldServer::create(
        bi_config,
        std::make_shared<CoroHandler>(),
        [](std::string error) {
            spdlog::error("coro fantasy::HelloWorldServer error: {}", error);
        });
    server->monitor(
        [](std::tuple<zmq_event_t, std::string> data) {
            auto& [event, point] = data;
            spdlog::info("bi coro server monitor: {} {}",frpc::getEventName(event.event), point);
        },
        ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED);
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
