#ifndef _FRPC_CONTEXT_POOL_H_
#define _FRPC_CONTEXT_POOL_H_

#include <asio.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace frpc {

class ContextPool final {
public:
    ContextPool(std::size_t pool_size)
        : m_next_io_context(0) {
        if (pool_size == 0)
            throw std::runtime_error("ContextPool size is 0");
        for (std::size_t i = 0; i < pool_size; ++i) {
            auto io_context_ptr = std::make_shared<asio::io_context>();
            m_io_contexts.emplace_back(io_context_ptr);
            m_work.emplace_back(
                asio::require(io_context_ptr->get_executor(), asio::execution::outstanding_work.tracked));
        }
    }

    void start() {
        for (auto& context : m_io_contexts)
            m_threads.emplace_back(std::jthread([&] {
                context->run();
            }));
    }

    void stop() {
        for (auto& context_ptr : m_io_contexts)
            context_ptr->stop();
    }

    asio::io_context& getIoContext() {
        size_t index = m_next_io_context.fetch_add(1, std::memory_order_relaxed);
        return *m_io_contexts[index % m_io_contexts.size()];
    }

private:
    std::vector<std::shared_ptr<asio::io_context>> m_io_contexts;
    std::list<asio::any_io_executor> m_work;
    std::atomic_uint64_t m_next_io_context;
    std::vector<std::jthread> m_threads;
};

} // namespace frpc

#endif // _FRPC_CONTEXT_POOL_H_
