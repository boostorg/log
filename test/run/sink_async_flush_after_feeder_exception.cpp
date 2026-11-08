/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE sink_async_flush_after_feeder_exception

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>

#include "test_async_sink.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

BOOST_AUTO_TEST_CASE(sink_async_flush_after_feeder_exception)
{
    using namespace async_sink_test;
    test_watchdog deadline;
    using sink_type = sinks::asynchronous_sink< controlled_backend, coordinated_queue< sinks::unbounded_fifo_queue > >;
    auto backend = boost::make_shared< controlled_backend >();
    sink_type sink(backend, false);
    test_barrier entered(2u), interrupted(2u);
    backend->consume_entered = &entered;
    backend->consume_resume = &interrupted;
    backend->fail_consume = true;
    sink.interrupted = &interrupted;
    sink.consume(make_record_view());
    sink.consume(make_record_view());

    std::thread feeder([&]()
    {
        try
        {
            sink.feed_records();
        }
        catch (std::runtime_error const&)
        {
        }
    });
    entered.arrive_and_wait();
    std::thread flusher([&]() { sink.flush(); });
    feeder.join();
    flusher.join();
    BOOST_CHECK_EQUAL(backend->consumed.load(std::memory_order_relaxed), 1u);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
