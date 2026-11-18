/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_unbounded_fifo_flush_exception.cpp
 * \author Andrey Semashev
 * \date   20.11.2026
 *
 * \brief  This file contains a test for asynchronous sink not getting stuck after a failed flush.
 *
 * The problem was reported in https://github.com/boostorg/log/pull/259.
 *
 * If processing of a log record threw an exception during a synchronous flush, before the flushing
 * loop was entered, the flush request flag was left set. As a result, subsequent calls to
 * `try_consume` kept rejecting records and calls to `consume` would block until the next successful
 * call to `flush`, even though the flush had been abandoned due to the exception.
 *
 * The test below enqueues a log record to a frontend without a dedicated feeding thread and flushes
 * it, expecting the exception thrown by the sink backend to propagate. After that, the test verifies
 * that the frontend still accepts new records through `try_consume` and feeds them to the backend
 * upon a subsequent call to `feed_records`.
 */

#define BOOST_TEST_MODULE sink_async_unbounded_fifo_flush_exception

#include <boost/log/core/record_view.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <stdexcept>

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>

#include "make_record.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

/*!
 * \brief A test sink backend that throws an exception on the next received log record, if armed
 *
 * The backend counts the log records it has received without throwing.
 */
class test_sink_backend :
    public sinks::basic_sink_backend< sinks::combine_requirements<
        sinks::synchronized_feeding,
        sinks::flushing
    >::type >
{
    //! Number of log records received without throwing
    unsigned int m_record_count;
    //! Whether the next received log record must throw
    bool m_throw_on_next;

public:
    test_sink_backend() : m_record_count(0u), m_throw_on_next(false)
    {
    }

    //! Makes the backend throw an exception on the next log record it receives
    void throw_on_next()
    {
        m_throw_on_next = true;
    }

    //! Returns the number of log records received
    unsigned int get_record_count() const
    {
        return m_record_count;
    }

    //! The method receives the log message
    void consume(logging::record_view const&)
    {
        if (m_throw_on_next)
        {
            m_throw_on_next = false;
            throw std::runtime_error("Simulated failure in the sink backend");
        }

        ++m_record_count;
    }

    //! The method implements flushing any buffers
    void flush() {}
};

BOOST_AUTO_TEST_CASE(sink_async_unbounded_fifo_flush_exception)
{
    auto backend = boost::make_shared< test_sink_backend >();
    // The sink is constructed without a dedicated feeding thread so that flush feeds records synchronously
    auto sink = boost::make_shared< sinks::asynchronous_sink< test_sink_backend, sinks::unbounded_fifo_queue > >(backend, false);

    // Enqueue a log record that the sink backend will throw on when flushed
    backend->throw_on_next();
    sink->consume(make_record_view());

    BOOST_CHECK_THROW(sink->flush(), std::runtime_error);

    // The abandoned flush request must not keep rejecting new log records
    BOOST_CHECK(sink->try_consume(make_record_view()));

    // The frontend must still be able to feed the buffered log records to the backend
    sink->feed_records();
    BOOST_CHECK_EQUAL(backend->get_record_count(), 1u);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
