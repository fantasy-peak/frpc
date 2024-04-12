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

struct Handler final : public frpc::HelloWorldServerHandler {
    virtual void hello_world(frpc::BankInfo bank_info,
                             std::string bank_name,
                             uint64_t blance,
                             std::function<void(std::string, frpc::Info, uint64_t)> cb) override {
        spdlog::info("frpc::HelloWorldServer server recv: {}, bank_name: {}, blance: {}",
                     frpc::toString(bank_info), bank_name, blance);
        frpc::Info info;
        info.name = "test";
        cb("hello world", std::move(info), 789);
    }
};

void start_server() {
    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
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
}

int main() {
    start(start_server);

    frpc::ChannelConfig bi_config{};
    bi_config.addr = addr;
    auto client = frpc::HelloWorldClient::create(bi_config, [](std::string error) {
        spdlog::error("frpc::HelloWorldClient error: {}", error);
    });
    client->start();

    client->hello_world(
        create_bank_info(),
        std::string{"frpc-bank"},
        999,
        [](std::string reply, frpc::Info info, uint64_t count) {
            spdlog::info("frpc::HelloWorldClient::hello_world recv: {},{},{}", reply, frpc::toString(info), count);
        });

    client->hello_world(
        create_bank_info(),
        std::string{"frpc-bank-1"},
        999,
        [](std::string reply, frpc::Info info, uint64_t count) {
            spdlog::info("frpc::HelloWorldClient::hello_world(timeout) recv: {},{},{}", reply, frpc::toString(info), count);
        },
        std::chrono::milliseconds(200),
        [] {
            spdlog::info("frpc::HelloWorldClient::timeout timeout!!!");
        });

    std::this_thread::sleep_for(std::chrono::seconds(10));

    return 0;
}
