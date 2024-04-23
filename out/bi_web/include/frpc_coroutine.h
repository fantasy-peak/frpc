#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <coroutine>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <type_traits>

namespace frpc {

namespace internal {

template <typename T>
auto getAwaiterImpl(T&& value) noexcept(
    noexcept(static_cast<T&&>(value).operator co_await()))
    -> decltype(static_cast<T&&>(value).operator co_await()) {
    return static_cast<T&&>(value).operator co_await();
}

template <typename T>
auto getAwaiterImpl(T&& value) noexcept(
    noexcept(operator co_await(static_cast<T&&>(value))))
    -> decltype(operator co_await(static_cast<T&&>(value))) {
    return operator co_await(static_cast<T&&>(value));
}

template <typename T>
auto getAwaiter(T&& value) noexcept(
    noexcept(getAwaiterImpl(static_cast<T&&>(value))))
    -> decltype(getAwaiterImpl(static_cast<T&&>(value))) {
    return getAwaiterImpl(static_cast<T&&>(value));
}

} // end namespace internal

template <typename T>
struct await_result {
    using awaiter_t = decltype(internal::getAwaiter(std::declval<T>()));
    using type = decltype(std::declval<awaiter_t>().await_resume());
};

template <typename T>
using await_result_t = typename await_result<T>::type;

template <typename T, typename = std::void_t<>>
struct is_awaitable : std::false_type {
};

template <typename T>
struct is_awaitable<
    T,
    std::void_t<decltype(internal::getAwaiter(std::declval<T>()))>>
    : std::true_type {
};

template <typename T>
constexpr bool is_awaitable_v = is_awaitable<T>::value;

struct final_awaiter {
    bool await_ready() noexcept {
        return false;
    }

    template <typename T>
    auto await_suspend(std::coroutine_handle<T> handle) noexcept {
        return handle.promise().continuation_;
    }

    void await_resume() noexcept {
    }
};

template <typename Promise>
struct task_awaiter {
    using handle_type = std::coroutine_handle<Promise>;

public:
    explicit task_awaiter(handle_type coro)
        : coro_(coro) {
    }

    bool await_ready() noexcept {
        return !coro_ || coro_.done();
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept {
        coro_.promise().setContinuation(handle);
        return coro_;
    }

    auto await_resume() {
        if constexpr (std::is_void_v<decltype(coro_.promise().result())>) {
            coro_.promise().result(); // throw exception if any
            return;
        } else {
            return std::move(coro_.promise().result());
        }
    }

private:
    handle_type coro_;
};

template <typename T = void>
struct [[nodiscard]] Task {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    Task(handle_type h)
        : coro_(h) {
    }

    Task(const Task&) = delete;

    Task(Task&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    ~Task() {
        if (coro_)
            coro_.destroy();
    }

    Task& operator=(const Task&) = delete;

    Task& operator=(Task&& other) noexcept {
        if (std::addressof(other) == this)
            return *this;
        if (coro_)
            coro_.destroy();

        coro_ = other.coro_;
        other.coro_ = nullptr;
        return *this;
    }

    struct promise_type {
        Task<T> get_return_object() {
            return Task<T>{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() {
            return {};
        }

        void return_value(const T& v) {
            value = v;
        }

        void return_value(T&& v) {
            value = std::move(v);
        }

        auto final_suspend() noexcept {
            return final_awaiter{};
        }

        void unhandled_exception() {
            exception_ = std::current_exception();
        }

        T&& result() && {
            if (exception_ != nullptr)
                std::rethrow_exception(exception_);
            assert(value.has_value() == true);
            return std::move(value.value());
        }

        T& result() & {
            if (exception_ != nullptr)
                std::rethrow_exception(exception_);
            assert(value.has_value() == true);
            return value.value();
        }

        void setContinuation(std::coroutine_handle<> handle) {
            continuation_ = handle;
        }

        std::optional<T> value;
        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
    };

    auto operator co_await() const noexcept {
        return task_awaiter(coro_);
    }

    handle_type coro_;
};

template <>
struct [[nodiscard]] Task<void> {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    Task(handle_type handle)
        : coro_(handle) {
    }

    Task(const Task&) = delete;

    Task(Task&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    ~Task() {
        if (coro_)
            coro_.destroy();
    }

    Task& operator=(const Task&) = delete;

    Task& operator=(Task&& other) noexcept {
        if (std::addressof(other) == this)
            return *this;
        if (coro_)
            coro_.destroy();

        coro_ = other.coro_;
        other.coro_ = nullptr;
        return *this;
    }

    struct promise_type {
        Task<> get_return_object() {
            return Task<>{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() {
            return {};
        }

        void return_void() {
        }

        auto final_suspend() noexcept {
            return final_awaiter{};
        }

        void unhandled_exception() {
            exception_ = std::current_exception();
        }

        void result() {
            if (exception_ != nullptr)
                std::rethrow_exception(exception_);
        }

        void setContinuation(std::coroutine_handle<> handle) {
            continuation_ = handle;
        }

        std::exception_ptr exception_;
        std::coroutine_handle<> continuation_;
    };

    auto operator co_await() const noexcept {
        return task_awaiter(coro_);
    }

    handle_type coro_;
};

struct AsyncTask {
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    AsyncTask() = default;

    AsyncTask(handle_type h)
        : coro_(h) {
    }

    AsyncTask(const AsyncTask&) = delete;

    AsyncTask(AsyncTask&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    AsyncTask& operator=(const AsyncTask&) = delete;

    AsyncTask& operator=(AsyncTask&& other) noexcept {
        if (std::addressof(other) == this)
            return *this;

        coro_ = other.coro_;
        other.coro_ = nullptr;
        return *this;
    }

    struct promise_type {
        AsyncTask get_return_object() noexcept {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() const noexcept {
            return {};
        }

        void unhandled_exception() {
            std::terminate();
        }

        void return_void() noexcept {
        }

        std::suspend_never final_suspend() const noexcept {
            return {};
        }
    };

    handle_type coro_;
};

template <typename Coro>
void async_run(Coro&& coro) {
    using CoroValueType = std::decay_t<Coro>;
    auto functor = [](CoroValueType coro) -> AsyncTask {
        auto frame = coro();

        using FrameType = std::decay_t<decltype(frame)>;
        static_assert(is_awaitable_v<FrameType>);

        co_await frame;
        co_return;
    };
    functor(std::forward<Coro>(coro));
}

template <typename Coro>
std::function<void()> async_func(Coro&& coro) {
    return [coro = std::forward<Coro>(coro)]() mutable {
        async_run(std::move(coro));
    };
}

template <typename T>
    requires(!std::is_reference<T>::value)
struct CallbackAwaiter {
public:
    using CallbackFunction =
        std::function<void(std::coroutine_handle<>, std::function<void(T)>)>;

    CallbackAwaiter(CallbackFunction callback_function)
        : callback_function_(std::move(callback_function)) {
    }

    bool await_ready() noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle) {
        callback_function_(handle, [this](T t) {
            result_ = std::move(t);
        });
    }

    T await_resume() noexcept {
        return std::move(result_);
    }

private:
    CallbackFunction callback_function_;
    T result_;
};

} // namespace frpc