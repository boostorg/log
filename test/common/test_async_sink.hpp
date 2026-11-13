/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_LOG_TEST_ASYNC_SINK_HPP_INCLUDED_
#define BOOST_LOG_TEST_ASYNC_SINK_HPP_INCLUDED_

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include <boost/log/core/record_view.hpp>

#include "test_barrier.hpp"
#include "test_sink_backend.hpp"
#include "test_watchdog.hpp"

namespace async_sink_test {

namespace logging = boost::log;

template< typename QueueT >
class coordinated_queue : public QueueT
{
public:
    test_barrier* dequeue_entered = nullptr;
    test_barrier* dequeue_resume = nullptr;
    test_barrier* interrupted = nullptr;

protected:
    coordinated_queue() = default;

    template< typename ArgsT >
    explicit coordinated_queue(ArgsT const& args) : QueueT(args) {}

    bool try_dequeue_ready(logging::record_view& rec)
    {
        const bool result = QueueT::try_dequeue_ready(rec);
        if (dequeue_entered)
        {
            auto* entered = dequeue_entered;
            dequeue_entered = nullptr;
            entered->arrive_and_wait();
            dequeue_resume->arrive_and_wait();
        }
        return result;
    }

    void interrupt_dequeue()
    {
        QueueT::interrupt_dequeue();
        if (interrupted)
        {
            auto* barrier = interrupted;
            interrupted = nullptr;
            barrier->arrive_and_wait();
        }
    }
};

class controlled_backend : public test_backend
{
public:
    std::atomic< unsigned int > consumed{0u};
    test_barrier* consume_entered = nullptr;
    test_barrier* consume_resume = nullptr;
    bool fail_consume = false;

    void consume(logging::record_view const&)
    {
        if (consume_entered)
        {
            auto* entered = consume_entered;
            consume_entered = nullptr;
            entered->arrive_and_wait();
            consume_resume->arrive_and_wait();
        }
        if (fail_consume)
        {
            fail_consume = false;
            throw std::runtime_error("Record processing failed");
        }
        consumed.fetch_add(1u, std::memory_order_relaxed);
    }
};

struct record_order
{
    using result_type = bool;
    bool operator()(logging::record_view const&, logging::record_view const&) const { return false; }
};

} // namespace async_sink_test

#endif // BOOST_LOG_TEST_ASYNC_SINK_HPP_INCLUDED_
