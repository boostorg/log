/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_ordering_flush_miss_unordered_record.cpp
 * \author Andrey Semashev
 * \date   20.11.2026
 *
 * \brief  This file tests that asynchronous_sink::flush does not complete before feeding
 * log records that are within the ordering window to the sink backend.
 *
 * This is a regression test for the bug fixed in https://github.com/boostorg/log/pull/259.
 *
 * When an ordering queueing strategy is used, log records that were enqueued less than the ordering
 * window ago are not ready for processing, and the record feeding thread skips these records when
 * draining the queue. Before the fix, the feeding thread would check whether a flush was requested
 * before trying to dequeue a ready log record, and after it found no ready records in the queue,
 * it would check the flush request flag again and flush the sink backend. If a flush was requested
 * between these two checks and there was a non-ready log record in the queue, the feeding thread
 * would flush the backend without feeding that log record first, and the flush call would return
 * while the log record was still buffered in the sink frontend.
 *
 * The race window is only a few nanoseconds wide, so this test runs a large number of attempts.
 * In every attempt it enqueues a log record that blocks the feeding thread in the sink backend,
 * enqueues another log record that stays within the ordering window, releases the backend and
 * waits for the feeding thread to exit the backend, and then requests a flush after a pseudo-random
 * delay. The delay sweeps the flush request through the time between the feeding thread exiting
 * the backend and the second flush request check in the draining loop, which is where the flush
 * request used to slip in. The test verifies that the log record within the ordering window has
 * been fed to the backend by the time the flush call returns.
 */

#define BOOST_TEST_MODULE sink_async_ordering_flush_miss_unordered_record

#include <boost/log/core/record_view.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <cstdint>
#include <chrono>
#include <atomic>
#include <thread>

#include <boost/config.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_ordering_queue.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/utility/record_ordering.hpp>
#include <boost/log/keywords/order.hpp>
#include <boost/log/keywords/ordering_window.hpp>

#include "make_record.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace attrs = boost::log::attributes;
namespace keywords = boost::log::keywords;

//! Number of attempts to hit the race between the flush request and the record feeding thread
constexpr unsigned int attempt_count = 30000u;

//! Ordering window duration. It must be longer than the time a log record stays in the queue
//! during an attempt, so that the record is not processed while the test thread is still in
//! the middle of the attempt.
constexpr auto ordering_window = std::chrono::microseconds(30);

//! Maximum pseudo-random delay, in iterations of a trivial loop, between the feeding thread
//! exiting the sink backend and requesting a flush
constexpr unsigned int max_spin = 2048u;

//! Timeout for waiting for the feeding thread to reach the expected state
constexpr auto wait_timeout = std::chrono::seconds(10);

/*!
 * \brief A simple pseudo-random number generator for generating delays with reproducible sequence
 */
class test_random
{
    std::uint32_t m_state;

public:
    explicit test_random(std::uint32_t seed = 12345u) : m_state(seed) {}

    unsigned int next()
    {
        // xorshift32
        m_state ^= m_state << 13;
        m_state ^= m_state >> 17;
        m_state ^= m_state << 5;
        return m_state;
    }
};

/*!
 * \brief An implementation of a test logging sink backend
 *
 * The sink backend blocks on log records with even values of the "Counter" attribute until it is
 * unblocked through \c open_gate. It reports when it enters and exits processing of a log record,
 * which is used by the test to time the flush request. It also remembers the value of the last
 * processed log record with an odd attribute value, which is used to detect that the record
 * has been fed to the backend.
 */
class test_sink_backend :
    public sinks::basic_sink_backend< sinks::combine_requirements<
        sinks::synchronized_feeding,
        sinks::flushing
    >::type >
{
    //! Base type
    typedef sinks::basic_sink_backend< sinks::combine_requirements<
        sinks::synchronized_feeding,
        sinks::flushing
    >::type > base_type;

    //! The value of the log record that is currently being processed, or 0 if none
    std::atomic< unsigned int > m_in_consume;
    //! Set when the processing of the log record is about to complete
    std::atomic< bool > m_consume_completed;
    //! The gate that blocks log records with even values
    std::atomic< bool > m_gate;
    //! The value of the last processed log record with an odd value
    std::atomic< unsigned int > m_last_odd_value;

public:
    test_sink_backend() :
        m_in_consume(0u),
        m_consume_completed(false),
        m_gate(true),
        m_last_odd_value(0u)
    {
    }

    //! The method receives a log record
    void consume(logging::record_view const& rec)
    {
        const unsigned int value = rec.attribute_values()["Counter"].extract_or_default(static_cast< unsigned int >(-1));

        m_in_consume.store(value, std::memory_order_relaxed);

        if ((value & 1u) == 0u)
        {
            // Block the thread that feeds log records until the gate is open
            while (!m_gate.load(std::memory_order_relaxed))
                std::this_thread::yield();
        }
        else
        {
            m_last_odd_value.store(value, std::memory_order_release);
        }

        m_in_consume.store(0u, std::memory_order_relaxed);
        m_consume_completed.store(true, std::memory_order_seq_cst);
    }

    //! The method implements flushing any buffers
    void flush()
    {
    }

    //! Opens the gate that blocks log records with even values
    void open_gate()
    {
        m_gate.store(true, std::memory_order_seq_cst);
    }

    //! Closes the gate that blocks log records with even values
    void close_gate()
    {
        m_gate.store(false, std::memory_order_seq_cst);
    }

    //! Returns the value of the log record that is currently being processed, or 0 if none
    unsigned int get_in_consume() const
    {
        return m_in_consume.load(std::memory_order_acquire);
    }

    //! Returns true if the feeding thread is about to complete processing of a log record
    bool consume_completed() const
    {
        return m_consume_completed.load(std::memory_order_seq_cst);
    }

    //! Resets the flag that is set by \c consume_completed
    void reset_consume_completed()
    {
        m_consume_completed.store(false, std::memory_order_seq_cst);
    }

    //! Returns the value of the last processed log record with an odd value
    unsigned int get_last_odd_value() const
    {
        return m_last_odd_value.load(std::memory_order_acquire);
    }
};

//! Waits until \c pred is true or the timeout expires. Returns false if the timeout expired.
template< typename Pred >
static bool wait_until(Pred&& pred, std::chrono::steady_clock::time_point const& deadline)
{
    while (!pred())
    {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

BOOST_AUTO_TEST_CASE(sink_async_ordering_flush)
{
    typedef logging::attribute_value_ordering< unsigned int > order_t;
    typedef sinks::asynchronous_sink< test_sink_backend, sinks::unbounded_ordering_queue< order_t > > sink_type;

    boost::shared_ptr< test_sink_backend > backend = boost::make_shared< test_sink_backend >();
    // The sink is constructed with a dedicated thread that feeds log records to the backend
    boost::shared_ptr< sink_type > sink = boost::make_shared< sink_type >
    (
        backend,
        keywords::order = order_t("Counter"),
        keywords::ordering_window = ordering_window
    );

    unsigned int error_count = 0u;
    test_random random;

    logging::attribute_set record_attrs;

    for (unsigned int attempt = 1u; attempt <= attempt_count; ++attempt)
    {
        backend->close_gate();
        backend->reset_consume_completed();

        // Enqueue a log record that will block the feeding thread in the sink backend
        const unsigned int blocking_value = 2u * attempt;
        record_attrs.clear();
        record_attrs.insert("Counter", attrs::make_constant(blocking_value));
        sink->consume(make_record_view(record_attrs));

        // Wait for the feeding thread to enter the sink backend with the blocking log record
        bool ok = wait_until([&] { return backend->get_in_consume() == blocking_value; },
                             std::chrono::steady_clock::now() + wait_timeout);
        if (!ok)
        {
            backend->open_gate();
            ++error_count;
            break;
        }

        // Unblock the feeding thread and enqueue another log record that will stay within the ordering
        // window for the rest of the attempt. The feeding thread is exiting the sink backend at this
        // point and is about to check the flush request flag in the draining loop.
        backend->open_gate();

        const unsigned int window_value = blocking_value + 1u;
        record_attrs.clear();
        record_attrs.insert("Counter", attrs::make_constant(window_value));
        sink->consume(make_record_view(record_attrs));

        // Wait for the feeding thread to complete processing of the blocking log record and apply
        // a pseudo-random delay before requesting a flush. Depending on the delay, the flush request
        // will be seen by the feeding thread before it checks the flush request flag in the draining
        // loop, within the race window, or after the flag check, in which case the log record in the
        // ordering window will be fed when the ordering window expires.
        ok = wait_until([&] { return backend->consume_completed(); },
                        std::chrono::steady_clock::now() + wait_timeout);
        if (!ok)
        {
            ++error_count;
            break;
        }

        const unsigned int spin = random.next() % max_spin;
        BOOST_ATTRIBUTE_UNUSED volatile unsigned int spin_sink = 0u;
        for (unsigned int i = 0u; i < spin; ++i)
            spin_sink = i;

        sink->flush();

        // The log record that was enqueued before the flush request must have been fed to the backend
        // by the time the flush returned. If the flush request slipped into the race window in the
        // draining loop, the backend will not have received the log record at this point.
        if (backend->get_last_odd_value() != window_value)
            ++error_count;
    }

    BOOST_CHECK_EQUAL(error_count, 0u);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
