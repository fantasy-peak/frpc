#ifndef _FRPC_MONITOR_H_
#define _FRPC_MONITOR_H_

#include <memory>
#include <functional>
#include <thread>

#include <zmq.hpp>
#include <zmq_addon.hpp>

namespace frpc {

inline std::string_view getEventName(uint16_t event_type) {
    switch (event_type) {
        case ZMQ_EVENT_CONNECTED:
            return "Connected";
        case ZMQ_EVENT_CONNECT_DELAYED:
            return "Connect Delayed";
        case ZMQ_EVENT_CONNECT_RETRIED:
            return "Connect Retried";
        case ZMQ_EVENT_LISTENING:
            return "Listening";
        case ZMQ_EVENT_BIND_FAILED:
            return "Bind Failed";
        case ZMQ_EVENT_ACCEPTED:
            return "Accepted";
        case ZMQ_EVENT_ACCEPT_FAILED:
            return "Accept Failed";
        case ZMQ_EVENT_CLOSED:
            return "Closed";
        case ZMQ_EVENT_CLOSE_FAILED:
            return "Close Failed";
        case ZMQ_EVENT_DISCONNECTED:
            return "Disconnected";
        case ZMQ_EVENT_MONITOR_STOPPED:
            return "Monitor Stopped";
        case ZMQ_EVENT_ALL:
            return "Monitor All";
        default:
            break;
    }
    return "Unknown";
}

class Monitor {
  public:
    Monitor(zmq::context_t& context, zmq::socket_t& socket)
        : m_context(context), m_socket(socket), m_endpoint(uniqueAddr()) {
    }

    virtual ~Monitor() {
        stop();
    }

    void start(
        std::function<void(std::optional<std::tuple<zmq_event_t, std::string>>)>
            call_back,
        int events = ZMQ_EVENT_ALL) {
        m_monitor_socket =
            std::make_unique<zmq::socket_t>(m_context, zmq::socket_type::pair);

        auto debug = std::getenv("DEBUG_EVENTS");
        if (debug)
            events = std::stoi(debug);

        int rc = zmq_socket_monitor(static_cast<void*>(m_socket),
                                    m_endpoint.c_str(),
                                    events);
        if (rc != 0) {
            throw ::zmq::error_t();
        }
        m_monitor_socket->connect(m_endpoint);
        m_monitor_call_back = std::move(call_back);

        m_thread = std::thread([&] {
            std::vector<zmq::pollitem_t> polls = {
                {static_cast<void*>(*(m_monitor_socket)), 0, ZMQ_POLLIN, 0},
            };

            while (m_activate.load(std::memory_order_acquire)) {
                zmq::poll(polls.data(), polls.size(), std::chrono::seconds(1));
                if (polls[0].revents & ZMQ_POLLIN)
                    m_monitor_call_back(getEvent());
            }
        });
    }

    auto& socket() {
        return m_monitor_socket;
    }

    void stop() {
        m_activate.store(false, std::memory_order_release);
        if (m_thread.joinable())
            m_thread.join();

        auto socket_ptr = static_cast<void*>(m_socket);
        if (socket_ptr)
            zmq_socket_monitor(socket_ptr, nullptr, 0);
    }

    std::optional<std::tuple<zmq_event_t, std::string>> getEvent() {
        zmq::message_t event_msg;
        if (!m_monitor_socket->recv(event_msg))
            return {};
        zmq::message_t addr_msg;
        if (!m_monitor_socket->recv(addr_msg))
            return {};

        zmq_event_t event;
        const char* data = static_cast<const char*>(event_msg.data());
        std::memcpy(&event.event, data, sizeof(uint16_t));
        std::memcpy(&event.value, data + sizeof(uint16_t), sizeof(int32_t));
        return std::make_tuple(event,
                               std::string(static_cast<const char*>(
                                               addr_msg.data()),
                                           addr_msg.size()));
    }

  private:
    zmq::context_t& m_context;
    zmq::socket_t& m_socket;
    std::string m_endpoint;
    std::unique_ptr<zmq::socket_t> m_monitor_socket;
    std::atomic<bool> m_activate{true};
    std::thread m_thread;
    std::function<void(std::optional<std::tuple<zmq_event_t, std::string>>)>
        m_monitor_call_back{nullptr};
};

}  // namespace frpc

#endif  // _FRPC_MONITOR_H_
