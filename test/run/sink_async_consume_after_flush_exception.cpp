/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE sink_async_consume_after_flush_exception

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>

#include "test_async_sink.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

BOOST_AUTO_TEST_CASE(sink_async_consume_after_flush_exception)
{
    using namespace async_sink_test;
    test_watchdog deadline;
    auto backend = boost::make_shared< controlled_backend >();
    sinks::asynchronous_sink< controlled_backend > sink(backend, false);
    backend->fail_consume = true;
    sink.consume(make_record_view());
    BOOST_CHECK_THROW(sink.flush(), std::runtime_error);

    auto rec = make_record_view();
    BOOST_CHECK(sink.try_consume(rec));
    std::thread producer([&]() { sink.consume(rec); });
    producer.join();
    sink.flush();
    BOOST_CHECK_EQUAL(backend->consumed.load(std::memory_order_relaxed), 2u);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
