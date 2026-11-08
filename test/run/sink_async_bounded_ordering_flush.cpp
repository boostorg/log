/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE sink_async_bounded_ordering_flush

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <chrono>
#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/bounded_ordering_queue.hpp>

#include "test_bounded_queue.hpp"
#include "test_watchdog.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

BOOST_AUTO_TEST_CASE(sink_async_bounded_ordering_flush_preserves_blocked_producer)
{
    test_watchdog deadline;
    using namespace bounded_queue_test;
    using queue_type = coordinated_bounded_queue< sinks::bounded_ordering_queue< record_order, 1u, coordinated_overflow > >;
    auto backend = boost::make_shared< controlled_backend >();
    sinks::asynchronous_sink< controlled_backend, queue_type > sink(backend,
        logging::keywords::order = record_order(), logging::keywords::ordering_window = std::chrono::milliseconds(0),
        logging::keywords::start_thread = false);
    check_flush_preserves_blocked_producer(sink, *backend);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
