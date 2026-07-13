/*
 *          Copyright 2026 - The MathWorks, Inc.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_frontend_flush.cpp
 * \author Conor Burgess
 * \date   08.07.2026
 *
 * \brief  This file contains a test for asynchronous_sink flushing.
 *
 * One thread repeatedly flushes the logging core while another logs, for a few
 * seconds. This stresses the handshake between asynchronous_sink::flush() (via
 * unbounded_fifo_queue::interrupt_dequeue) and the sink feeding thread parked
 * in the internal event. A lost-wakeup regression in that event made flush()
 * hang forever on this configuration. A single run rarely trips it, so the
 * test is meant to be looped; if the wakeup is lost the flush thread never
 * joins and the test hangs, which is itself the failure signal.
 */

#define BOOST_TEST_MODULE sink_async_frontend_flush

#include <boost/log/detail/config.hpp>

#include <boost/test/unit_test.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <atomic>
#include <chrono>
#include <thread>
#include <sstream>

#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include <boost/log/core/core.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include "test_barrier.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;

// One thread flushes the core while another logs, for a few seconds, both
// started together via a barrier. If the flush/feed handshake loses a wakeup,
// flush() hangs and the flush thread never joins.
BOOST_AUTO_TEST_CASE(async_flush_no_lost_wakeup)
{
    typedef sinks::asynchronous_sink< sinks::text_ostream_backend > sink_t;

    boost::shared_ptr< std::ostream > strm(new std::ostringstream());
    boost::shared_ptr< sink_t > sink = boost::make_shared< sink_t >();
    sink->locked_backend()->add_stream(strm);
    logging::core::get()->add_sink(sink);

    const int run_seconds = 5;
    std::atomic< bool > stop(false);
    test_barrier barrier(2u);

    std::thread flusher([&]()
    {
        barrier.arrive_and_wait();
        while (!stop.load(std::memory_order_relaxed))
            logging::core::get()->flush();
    });

    barrier.arrive_and_wait();
    src::severity_logger< int > lg;
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast< std::chrono::seconds >(
               std::chrono::steady_clock::now() - start).count() < run_seconds)
    {
        BOOST_LOG_SEV(lg, 0) << "stress";
        std::this_thread::yield();
    }

    stop.store(true, std::memory_order_relaxed);
    flusher.join();

    logging::core::get()->remove_all_sinks();

    BOOST_CHECK(true);
}

#else // !defined(BOOST_LOG_NO_THREADS)

BOOST_AUTO_TEST_CASE(async_flush_no_lost_wakeup)
{
    BOOST_CHECK(true);
}

#endif // !defined(BOOST_LOG_NO_THREADS)
