/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_unbounded_fifo_flush_deadlock.cpp
 * \author Andrey Semashev
 * \date   16.08.2026
 *
 * \brief  This file contains a test for the `asynchronous_sink::flush` deadlock fix.
 *
 * The problem was reported in https://github.com/boostorg/log/issues/255.
 *
 * The `event` implementation that was used to block the feeding thread in `unbounded_fifo_queue::dequeue_ready`
 * did not properly guarantee that the stores in `unbounded_fifo_queue::interrupt_dequeue` prior to
 * signalling the event were visible to the blocked thread upon wakeup. This resulted in a lost interruption
 * of the feeding thread, which ended up blocked waiting for new log records, and the other threads calling
 * `asynchronous_sink::flush` or `stop` ended up waiting for the respective operations to complete.
 *
 * The test below feeds a number of log records to a dummy sink using the `asynchronous_sink<unbounded_fifo_queue>`
 * frontend, while concurrently flushing it in another thread. The test is expected to not deadlock at the end.
 */

#define BOOST_TEST_MODULE sink_async_unbounded_fifo_flush_deadlock

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <atomic>
#include <chrono>
#include <thread>

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include "test_thread.hpp"
#include "test_barrier.hpp"
#include "test_sink_backend.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;

//! Duration of the loop of emitting log records and flushing
constexpr auto loop_duration = std::chrono::seconds(5);

//! Timeout for joining the thread that flushes log records
constexpr auto join_timeout = std::chrono::seconds(5);

BOOST_AUTO_TEST_CASE(sink_async_unbounded_fifo_flush_deadlock)
{
    auto core = logging::core::get();
    core->add_sink(boost::make_shared< sinks::asynchronous_sink< test_backend, sinks::unbounded_fifo_queue > >());

    std::atomic< bool > stop(false);
    test_barrier barrier(3u);

    test_thread flusher([&]()
    {
        barrier.arrive_and_wait();

        while (!stop.load(std::memory_order_relaxed))
            core->flush();
    });

    test_thread log_emitter([&]()
    {
        src::logger lg;

        barrier.arrive_and_wait();

        while (!stop.load(std::memory_order_relaxed))
        {
            BOOST_LOG(lg) << "stress";
            std::this_thread::yield();
        }
    });

    barrier.arrive_and_wait();

    std::this_thread::sleep_for(loop_duration);

    stop.store(true, std::memory_order_relaxed);

    const auto deadline = std::chrono::steady_clock::now() + join_timeout;

    bool flusher_joined = flusher.try_join_until(deadline);
    BOOST_CHECK(flusher_joined);
    if (!flusher_joined)
        flusher.detach();

    bool log_emitter_joined = log_emitter.try_join_until(deadline);
    BOOST_CHECK(log_emitter_joined);
    if (!log_emitter_joined)
        log_emitter.detach();
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
