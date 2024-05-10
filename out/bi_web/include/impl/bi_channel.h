#ifndef _FRPC_BI_CHANNEL_H_
#define _FRPC_BI_CHANNEL_H_

#include <memory>
#include <functional>
#include <thread>

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include "utils.h"

namespace frpc {

class BiChannel final {
  public:
    BiChannel(const ChannelConfig& config,
              std::function<void(std::string)> error,
              std::function<void(std::vector<zmq::message_t>&)> cb)
        : m_context_ptr(std::make_shared<zmq::context_t>(config.io_threads)),
          m_socket_ptr(
              std::make_shared<zmq::socket_t>(*m_context_ptr, config.socktype)),
          m_send(*m_context_ptr, zmq::socket_type::push),
          m_recv(*m_context_ptr, zmq::socket_type::pull),
          m_error(std::move(error)),
          m_cb(std::move(cb)) {
        init_socket(config);
    }

    BiChannel(const ChannelConfig& config,
              const std::shared_ptr<zmq::context_t>& context_ptr,
              std::function<void(std::string)> error,
              std::function<void(std::vector<zmq::message_t>&)> cb)
        : m_context_ptr(context_ptr),
          m_socket_ptr(
              std::make_shared<zmq::socket_t>(*m_context_ptr, config.socktype)),
          m_send(*m_context_ptr, zmq::socket_type::push),
          m_recv(*m_context_ptr, zmq::socket_type::pull),
          m_error(std::move(error)),
          m_cb(std::move(cb)) {
        init_socket(config);
    }

    BiChannel(const ChannelConfig& config,
              const std::shared_ptr<zmq::context_t>& context_ptr,
              const std::shared_ptr<zmq::socket_t>& socket_ptr,
              std::function<void(std::string)> error,
              std::function<void(std::vector<zmq::message_t>&)> cb)
        : m_context_ptr(context_ptr),
          m_socket_ptr(socket_ptr),
          m_send(*m_context_ptr, zmq::socket_type::push),
          m_recv(*m_context_ptr, zmq::socket_type::pull),
          m_error(std::move(error)),
          m_cb(std::move(cb)) {
        init_socket(config);
    }

    ~BiChannel() {
        m_running.store(false, std::memory_order_release);
        if (m_thread.joinable())
            m_thread.join();
        if (m_monitor_socket_ptr) {
            auto socket_ptr = static_cast<void*>(m_monitor_socket_ptr.get());
            zmq_socket_monitor(socket_ptr, nullptr, 0);
        }
    }

    template <typename T>
    void send(T&& snd_msgs) {
        std::lock_guard lk(m_mutex);
        if (!zmq::send_multipart(m_send,
                                 std::forward<decltype(snd_msgs)>(snd_msgs))) {
            m_error(FRPC_ERROR_FORMAT("send error!!!"));
        }
    }

    template <typename T>
    void send(T&& snd_msgs,
              const std::chrono::milliseconds& timeout,
              std::function<void()> cb) {
        std::lock_guard lk(m_mutex);
        if (!zmq::send_multipart(m_send,
                                 std::forward<decltype(snd_msgs)>(snd_msgs))) {
            m_error(FRPC_ERROR_FORMAT("send error!!!"));
        }
        auto timeout_point = std::chrono::system_clock::now() + timeout;
        m_timeout_task.emplace(timeout_point, std::move(cb));
    }

    bool monitor(std::function<void(std::tuple<zmq_event_t, std::string>)> cb,
                 int events = ZMQ_EVENT_ALL) {
        if (m_monitor_socket_ptr || m_running.load())
            return true;
        m_monitor_socket_ptr =
            std::make_shared<zmq::socket_t>(*m_context_ptr,
                                            zmq::socket_type::pair);
        auto endpoint = uniqueAddr();
        int rc = zmq_socket_monitor(static_cast<void*>(*m_socket_ptr),
                                    endpoint.c_str(),
                                    events);
        if (rc != 0) {
            return false;
        }
        m_monitor_socket_ptr->connect(endpoint);
        m_monitor_callback = std::move(cb);
        return true;
    }

    void start() {
        m_running = true;
        m_thread = std::thread([this] {
            std::vector<zmq::pollitem_t> items{
                {static_cast<void*>(*m_socket_ptr),
                 0,
                 ZMQ_POLLIN | ZMQ_POLLERR,
                 0},
                {static_cast<void*>(m_recv), 0, ZMQ_POLLIN | ZMQ_POLLERR, 0},
            };
            if (m_monitor_socket_ptr)
                items.emplace_back(
                    zmq::pollitem_t{static_cast<void*>(*(m_monitor_socket_ptr)),
                                    0,
                                    ZMQ_POLLIN,
                                    0});
            std::chrono::milliseconds interval(100);
            std::multimap<std::chrono::system_clock::time_point,
                          std::function<void()>>
                timeout_task;
            std::vector<zmq::message_t> recv_msgs;
            recv_msgs.reserve(4);
            while (m_running.load(std::memory_order_acquire)) {
                zmq::poll(items, interval);
                if (items[0].revents & ZMQ_POLLIN) {
                    recv_msgs.clear();
                    auto ret =
                        zmq::recv_multipart(*m_socket_ptr,
                                            std::back_inserter(recv_msgs));
                    if (!ret) {
                        m_error(
                            FRPC_ERROR_FORMAT("zmq::recv_multipart error!!!"));
                        break;
                    }
                    m_cb(recv_msgs);
                }
                if (items[1].revents & ZMQ_POLLIN) {
                    try {
                        recv_msgs.clear();
                        auto ret =
                            zmq::recv_multipart(m_recv,
                                                std::back_inserter(recv_msgs));
                        if (!ret) {
                            m_error(FRPC_ERROR_FORMAT(
                                "recv zmq::recv_multipart error!!!"));
                            break;
                        }
                        if (!zmq::send_multipart(*m_socket_ptr, recv_msgs)) {
                            m_error(FRPC_ERROR_FORMAT(
                                "zmq::send_multipart error!!!"));
                            break;
                        }
                    } catch (const zmq::error_t& e) {
                        m_error(FRPC_ERROR_FORMAT(
                            std::string{"BiChannel error "} + e.what()));
                    }
                }
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    if (!m_timeout_task.empty()) {
                        timeout_task.merge(m_timeout_task);
                        m_timeout_task.clear();
                    }
                }
                auto now = std::chrono::system_clock::now();
                while (!timeout_task.empty() &&
                       timeout_task.begin()->first <= now) {
                    (timeout_task.begin()->second)();
                    timeout_task.erase(timeout_task.begin());
                }
                if (m_monitor_socket_ptr && (items[2].revents & ZMQ_POLLIN)) {
                    zmq::message_t event_msg;
                    if (!m_monitor_socket_ptr->recv(event_msg))
                        continue;
                    zmq::message_t addr_msg;
                    if (!m_monitor_socket_ptr->recv(addr_msg))
                        continue;
                    zmq_event_t event;
                    const char* data =
                        static_cast<const char*>(event_msg.data());
                    std::memcpy(&event.event, data, sizeof(uint16_t));
                    std::memcpy(&event.value,
                                data + sizeof(uint16_t),
                                sizeof(int32_t));
                    m_monitor_callback(std::make_tuple(
                        event,
                        std::string(static_cast<const char*>(addr_msg.data()),
                                    addr_msg.size())));
                }
            }
        });
    }

    auto& socket() {
        return m_socket_ptr;
    }

    auto& context() {
        return m_context_ptr;
    }

  private:
    void init_socket(const ChannelConfig& config) {
        m_socket_ptr->set(zmq::sockopt::sndhwm, config.sendhwm);
        m_socket_ptr->set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_socket_ptr->set(zmq::sockopt::sndbuf, config.sendbuf);
        m_socket_ptr->set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_socket_ptr->set(zmq::sockopt::linger, config.linger);
        if (config.tcp_keepalive) {
            m_socket_ptr->set(zmq::sockopt::tcp_keepalive, 1);
            m_socket_ptr->set(zmq::sockopt::tcp_keepalive_idle,
                              config.tcp_keepalive_idle);
            m_socket_ptr->set(zmq::sockopt::tcp_keepalive_cnt,
                              config.tcp_keepalive_cnt);
            m_socket_ptr->set(zmq::sockopt::tcp_keepalive_intvl,
                              config.tcp_keepalive_intvl);
        }
        if (config.probe) {
            m_socket_ptr->set(zmq::sockopt::probe_router, 1);
        }
        if (config.bind) {
            if (config.socktype == zmq::socket_type::router) {
                if (config.mandatory)
                    m_socket_ptr->set(zmq::sockopt::router_mandatory, 0);
                else
                    m_socket_ptr->set(zmq::sockopt::router_mandatory, 1);
            }
            m_socket_ptr->bind(config.addr);
        } else {
            m_socket_ptr->connect(config.addr);
        }
        auto addr = uniqueAddr();
        m_recv.set(zmq::sockopt::rcvhwm, config.recvhwm);
        m_recv.set(zmq::sockopt::rcvbuf, config.recvbuf);
        m_recv.bind(addr);

        m_send.set(zmq::sockopt::sndhwm, config.sendhwm);
        m_send.set(zmq::sockopt::sndbuf, config.sendbuf);
        m_send.set(zmq::sockopt::linger, config.linger);

        m_send.connect(addr);
    }

    std::shared_ptr<zmq::context_t> m_context_ptr;
    std::shared_ptr<zmq::socket_t> m_socket_ptr;
    zmq::socket_t m_send;
    zmq::socket_t m_recv;
    std::function<void(std::string)> m_error;
    std::function<void(std::vector<zmq::message_t>&)> m_cb;
    std::thread m_thread;
    std::atomic_bool m_running{false};
    std::mutex m_mutex;
    std::multimap<std::chrono::system_clock::time_point, std::function<void()>>
        m_timeout_task;
    std::function<void(std::tuple<zmq_event_t, std::string>)>
        m_monitor_callback;
    std::shared_ptr<zmq::socket_t> m_monitor_socket_ptr{nullptr};
};

}  // namespace frpc

#endif  //_FRPC_BI_CHANNEL_H_
