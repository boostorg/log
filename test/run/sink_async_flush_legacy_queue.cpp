/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE sink_async_flush_legacy_queue

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <thread>
#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>

#include "test_barrier.hpp"
#include "test_sink_backend.hpp"
#include "test_watchdog.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

class legacy_queue : public sinks::unbounded_fifo_queue
{
public:
    test_barrier* dequeue_entered = nullptr;
    bool last_wake_argument = true;

protected:
    bool dequeue_ready(logging::record_view& rec)
    {
        if (dequeue_entered)
        {
            auto* barrier = dequeue_entered;
            dequeue_entered = nullptr;
            barrier->arrive_and_wait();
        }
        return sinks::unbounded_fifo_queue::dequeue_ready(rec);
    }

    void interrupt_dequeue(bool wake = true)
    {
        last_wake_argument = wake;
        if (wake)
            sinks::unbounded_fifo_queue::interrupt_dequeue();
    }
};

BOOST_AUTO_TEST_CASE(sink_async_flush_legacy_queue)
{
    test_watchdog deadline;
    auto backend = boost::make_shared< test_backend >();
    sinks::asynchronous_sink< test_backend, legacy_queue > sink(backend, false);
    test_barrier entered(2u);
    sink.dequeue_entered = &entered;

    std::thread feeder([&]() { sink.run(); });
    entered.arrive_and_wait();
    sink.flush();
    BOOST_CHECK(sink.last_wake_argument);
    sink.stop();
    feeder.join();
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
