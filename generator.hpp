#pragma once


#include <experimental/coroutine>

template <class T>
struct generator {

    struct promise_type;
    using handle_type = std::experimental::coroutine_handle<promise_type>;

    generator(handle_type h) : h{h}
    {
    }
    ~generator()
    {
        if (h) {
            h.destroy();
        }
    }

    struct promise_type {
        const T* value_ptr = nullptr;
        bool finished = false;
        auto get_return_object()
        {
            return generator<T>{handle_type::from_promise(*this)};
        }
        std::experimental::suspend_always initial_suspend() { return {}; }
        std::experimental::suspend_always final_suspend() { return {}; }
        void return_void()
        {
            finished = true;
            value_ptr = nullptr;
        }
        std::experimental::suspend_always yield_value(const T& value) noexcept
        {
            value_ptr = std::addressof(value);
            return {};
        }

        std::experimental::suspend_always yield_value() noexcept
        {
            static_assert(std::is_void_v<T>);
            return {};
        }

        void unhandled_exception() { std::terminate(); }
    };

    handle_type h;
    generator() : h{} {}
    generator(const generator&) = delete;
    generator(generator&& g) : h{g.h}
    {
        g.h = nullptr;
    }
    const T& current() const
    {
        assert(h != nullptr && h.promise().value_ptr != nullptr);

        return *h.promise().value_ptr;
    }
    const T* next()
    {
        if (!h) {
            return nullptr;
        }

        if (!h.promise().finished) {
            h.resume();
        }
        return h.promise().value_ptr;
    }

    bool finished() const
    {
        return !h || h.promise().finished;
    }

    struct iterator {

        struct end {
        };
        generator& g;
        iterator(generator& g) : g{g} { g.next(); }
        iterator& operator++()
        {
            g.next();
            return *this;
        }
        const T& operator*() const { return g.current(); }

        bool operator==(end) const
        {
            return g.finished();
        }

        bool operator!=(end) const
        {
            return !g.finished();
        }
    };
    iterator begin() &
    {
        return iterator{*this};
    }
    constexpr typename iterator::end end() const
    {
        return {};
    }
};