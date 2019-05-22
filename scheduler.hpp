#pragma once

#include <experimental/coroutine>
#include <future>
#include <iostream>
#include <queue>

struct Schedule {
    double time;
    std::function<void()> task;
    bool operator>(const Schedule& s2) const { return time > s2.time; }
};
namespace Global
{

double t = 0;

std::priority_queue<Schedule, std::vector<Schedule>, std::greater<>> queue;

}  // namespace Global


template <class T>
struct future {

    template <class U>
    struct promise_type_derived;
    using promise_type = promise_type_derived<T>;
    using handle_type = std::experimental::coroutine_handle<promise_type>;


    future(std::future<T>&& f, handle_type h) : f{std::move(f)}, h{h}
    {
    }
    ~future()
    {
        if (h) {
            //            h.destroy();
        }
    }


    struct promise_type_base {
        std::promise<T> promise;

        std::function<void()> callback;

        auto get_return_object()
        {
            return future<T>{promise.get_future(), handle_type::from_promise(*static_cast<promise_type*>(this))};
        }
        std::experimental::suspend_never initial_suspend() { return {}; }
        std::experimental::suspend_never final_suspend() { return {}; }

        void unhandled_exception() { std::terminate(); }


        template <class U>
        auto await_transform(future<U>&& f2)
        {
            struct awaiter {
                future<U> f;
                bool await_ready() { return f.done(); }
                void await_suspend(handle_type h)
                {
                    f.h.promise().callback = [h]() mutable {if(!h.done()){h.resume();} };
                }
                U await_resume() { return f.get(); }
            };
            return awaiter{std::move(f2)};
        }

        auto await_transform(double d)
        {
            struct awaiter {
                double d;
                bool await_ready() { return d < 0; }
                void await_suspend(handle_type h)
                {
                    // 指定時間経過後にコルーチンをresumeするワーカスレッドを生成。
                    Global::queue.push({Global::t + d, [h]() mutable {
                                            if (!h.done())
                                                h.resume();
                                            else {
                                                std::cout << Global::t << ": done" << std::endl;
                                            }
                                        }});
                }
                void await_resume() {}
            };
            return awaiter{d};
        }
    };

    template <class U>
    struct promise_type_derived : promise_type_base {
        void return_value(const T& x)
        {
            this->promise.set_value(x);
            if (this->callback != nullptr) {
                this->callback();
            }
        }
    };

    template <>
    struct promise_type_derived<void> : promise_type_base {
        void return_void()
        {
            this->promise.set_value();
            if (this->callback != nullptr) {
                this->callback();
            }
        }
    };


    std::future<T> f;
    handle_type h;
    future(const future&) = delete;
    future(future&& g) : f{std::move(g.f)}, h{g.h}
    {
        g.h = nullptr;
    }

    T get()
    {
        return f.get();
    }

    bool done() const
    {
        using namespace std::literals;
        return f.wait_for(0s) == std::future_status::ready;
    }
};