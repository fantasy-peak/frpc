#include <functional>
#include <string>

#include <spdlog/spdlog.h>
#include <zmq.h>

#include "fantasy.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

#ifdef __cpp_impl_coroutine
struct CoroHelloWorldReceiver final : public fantasy::AsioCoroHelloWorldReceiverHandler {
    virtual asio::awaitable<void> hello_world(std::string in) noexcept override {
        spdlog::info("CoroHelloWorldReceiver::hello_world: {}", in);
        co_return;
    }
    virtual asio::awaitable<void> notice(int32_t in, std::string info) noexcept override {
        spdlog::info("CoroHelloWorldReceiver::notice: {}: {}", in, info);
        co_return;
    }
};
#else
struct HelloWorldReceiverHandler final : public fantasy::HelloWorldReceiverHandler {
    HelloWorldReceiverHandler() = default;
    HelloWorldReceiverHandler(const std::string& label)
        : label(label) {
    }

    virtual void hello_world(std::string in) noexcept override {
        spdlog::info("HelloWorldReceiverHandler::hello_world: {}, {}", label, in);
        return;
    }
    virtual void notice(int32_t in, std::string info) noexcept override {
        spdlog::info("HelloWorldReceiverHandler::notice: {}, {}: {}", label, in, info);
        return;
    }

    std::string label{"test"};
};
#endif

void start_publisher() {
    frpc::ChannelConfig pub_config{};
    pub_config.addr = addr;
    pub_config.socktype = zmq::socket_type::pub;
    auto sender = fantasy::HelloWorldSender::create(pub_config);

    int i = 10;
    while (i--) {
        sender->hello_world(std::to_string(i) + "_frpc");
        sender->notice(i, "hello world");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int main() {
    start(start_publisher);

    frpc::ChannelConfig sub_config{};
    sub_config.socktype = zmq::socket_type::sub;
    sub_config.addr = addr;

    auto receiver = fantasy::HelloWorldReceiver::create(
        sub_config,
#ifdef __cpp_impl_coroutine
        std::make_shared<CoroHelloWorldReceiver>(),
#else
        std::make_shared<HelloWorldReceiverHandler>(),
#endif
        [](auto error) {
            spdlog::error("{}", error);
        });
    receiver->monitor(
        [](std::tuple<zmq_event_t, std::string> data) {
            auto& [event, point] = data;
            spdlog::info("HelloWorldReceiver monitor: {} {}", frpc::getEventName(event.event), point);
        },
        ZMQ_EVENT_CONNECTED | ZMQ_EVENT_DISCONNECTED);
    receiver->start();

    std::this_thread::sleep_for(std::chrono::seconds(10));

    return 0;
}
