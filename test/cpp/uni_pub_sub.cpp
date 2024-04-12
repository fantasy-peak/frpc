#include <functional>
#include <string>

#include <spdlog/spdlog.h>

#include "frpc.hpp"

inline std::string addr{"tcp://127.0.0.1:5878"};

void start(std::function<void()> func) {
    std::thread(std::move(func)).detach();
}

#ifdef __cpp_impl_coroutine
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
#else
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
#endif

void start_publisher() {
    frpc::ChannelConfig pub_config{};
    pub_config.addr = addr;
    pub_config.socktype = zmq::socket_type::pub;
    auto sender = frpc::HelloWorldSender::create(pub_config);

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

    std::this_thread::sleep_for(std::chrono::seconds(10));

    return 0;
}
