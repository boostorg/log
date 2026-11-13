/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_bounded_flush_loss.cpp
 * \author Andrey Semashev
 * \date   12.11.2026
 *
 * \brief  This file contains a test for not losing log records on flush from an asynchronous sink with a bounded queue.
 *
 * The problem was reported in https://github.com/boostorg/log/pull/260.
 *
 * In an asynchronous sink, when a bounded queue was full and another log record was blocked at insertion into the queue,
 * and another thread flushed the sink, the blocked log record was dropped instead of being enqueued after the flush is complete.
 *
 * The test below feeds three log records to a bounded sink with a queue capacity of 1. The sink backend blocks upon receiving
 * the first record, the second record stays in the frontend queue and the third one blocks on enqueue. At that point, flush is
 * invoked. Then the sink backend unblocks and processes all records there are left. The test succeeds if the backend receives all
 * three log records. The test runs with all bounded queue strategies with `block_on_overflow` policy.
 */

#define BOOST_TEST_MODULE sink_async_bounded_flush_loss

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <utility>

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/core/record_view.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/bounded_fifo_queue.hpp>
#include <boost/log/sinks/bounded_ordering_queue.hpp>
#include <boost/log/sinks/block_on_overflow.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/utility/record_ordering.hpp>
#include <boost/log/keywords/order.hpp>

#include "test_thread.hpp"
#include "test_barrier.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

//! Timeout for joining threads
constexpr auto join_timeout = std::chrono::seconds(5);

//! Timeout for waiting before passing to the next test state
constexpr auto step_duration = std::chrono::milliseconds(500);

class test_sink_backend :
    public sinks::basic_sink_backend< sinks::synchronized_feeding >
{
    //! Base type
    using base_type = sinks::basic_sink_backend< sinks::synchronized_feeding >;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_condition_variable;
    unsigned int m_record_count;
    unsigned int m_error_count;
    bool m_block;

public:
    test_sink_backend() : m_record_count(0u), m_error_count(0u), m_block(true)
    {
    }

    //! The method receives the log message
    void consume(logging::record_view const& rec)
    {
        std::unique_lock< std::mutex > lock(m_mutex);

        while (m_block)
            m_condition_variable.wait(lock);

        unsigned int value = rec.attribute_values()["Counter"].extract_or_default(static_cast< unsigned int >(-1));
        if (value != m_record_count)
        {
            ++m_error_count;
        }

        ++m_record_count;
    }

    //! Unblocks the thread that feeds log records
    void unblock()
    {
        std::lock_guard< std::mutex > lock(m_mutex);
        m_block = false;
        m_condition_variable.notify_one();
    }

    //! Returns the number of log records received
    unsigned int get_received_count() const
    {
        std::lock_guard< std::mutex > lock(m_mutex);
        return m_record_count;
    }

    //! Returns the number of detected errors
    unsigned int get_error_count() const
    {
        std::lock_guard< std::mutex > lock(m_mutex);
        return m_error_count;
    }
};

template< typename QueueingPolicy, typename... Args >
void test_async_bounded_flush(Args&&... args)
{
    auto core = logging::core::get();
    auto backend = boost::make_shared< test_sink_backend >();
    auto sink = boost::make_shared< sinks::asynchronous_sink< test_sink_backend, QueueingPolicy > >(backend, std::forward< Args >(args)...);
    core->add_sink(sink);

    test_barrier unblock_barrier(2u);
    test_barrier flush1_barrier(2u);
    test_barrier flush2_barrier(2u);

    test_thread emitter([&]()
    {
        src::logger lg;

        for (unsigned int i = 0u; i < 3u; ++i)
        {
            auto attr(logging::add_scoped_logger_attribute(lg, "Counter", attrs::make_constant(i)));
            BOOST_LOG(lg) << i;

            if (i == 1u)
                flush1_barrier.arrive_and_wait(); // notify the flusher
        }
    });

    test_thread flusher([&]()
    {
        flush1_barrier.arrive_and_wait();

        // Wait fo the emitter to block on the third log record
        std::this_thread::sleep_for(step_duration);

        unblock_barrier.arrive_and_wait(); // notify the unblocker
        core->flush();

        flush2_barrier.arrive_and_wait();
        // Do the final flush
        core->flush();
    });

    test_thread unblocker([&]()
    {
        unblock_barrier.arrive_and_wait();

        // Wait for the flusher to block in flush
        std::this_thread::sleep_for(step_duration);

        backend->unblock();
    });

    const auto deadline = std::chrono::steady_clock::now() + join_timeout;

    bool emitter_joined = emitter.try_join_until(deadline);
    BOOST_CHECK(emitter_joined);
    if (!emitter_joined)
        emitter.detach();

    bool unblocker_joined = unblocker.try_join_until(deadline);
    BOOST_CHECK(unblocker_joined);
    if (!unblocker_joined)
        unblocker.detach();

    // Let the log records be processed naturally and notifu the flusher
    std::this_thread::sleep_for(step_duration);
    flush2_barrier.arrive_and_wait();

    bool flusher_joined = flusher.try_join_until(deadline);
    BOOST_CHECK(flusher_joined);
    if (!flusher_joined)
        flusher.detach();

    BOOST_CHECK_EQUAL(backend->get_received_count(), 3u);
    BOOST_CHECK_EQUAL(backend->get_error_count(), 0u);

    core->remove_sink(sink);
}

BOOST_AUTO_TEST_CASE(sink_async_bounded_flush_fifo)
{
    test_async_bounded_flush< sinks::bounded_fifo_queue< 1u, sinks::block_on_overflow > >();
}

BOOST_AUTO_TEST_CASE(sink_async_bounded_flush_ordering)
{
    using order_t = logging::attribute_value_ordering< unsigned int >;
    test_async_bounded_flush< sinks::bounded_ordering_queue< order_t, 1u, sinks::block_on_overflow > >(keywords::order = order_t("Counter"));
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
