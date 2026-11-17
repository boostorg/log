/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_unbounded_fifo_flush_feeding_exception.cpp
 * \author Andrey Semashev
 * \date   20.11.2026
 *
 * \brief  This file contains a test for asynchronous_sink::flush not returning
 * when the record feeding operation fails with an exception.
 *
 * The problem was reported in https://github.com/boostorg/log/pull/259.
 *
 * If `asynchronous_sink::flush` is called while another thread runs a record feeding
 * operation, the caller blocks, expecting the feeding thread to perform the flush.
 * If the feeding operation failed with an exception (e.g. thrown from the sink backend
 * when processing a log record) before it entered the flushing loop, the flush request
 * was never completed, and the thread blocked in `flush` remained blocked forever.
 * With the fix, the blocked thread is unblocked when the feeding operation ends, and
 * the flush is performed synchronously by the thread that requested it.
 *
 * The test below blocks the sink backend while it processes the first log record,
 * requests a flush from another thread and then releases the backend to throw. The test
 * verifies that `flush` returns and performs the flush synchronously, feeding the
 * buffered log records to the backend.
 */

#define BOOST_TEST_MODULE sink_async_unbounded_fifo_flush_feeding_exception

#include <boost/log/core/record_view.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>

#include "make_record.hpp"
#include "test_thread.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

//! Timeout for joining the threads that flush log records and feed them to the backend
constexpr auto join_timeout = std::chrono::seconds(10);

/*!
 * \brief A test sink backend that blocks on the first received log record and throws
 * an exception when unblocked. Subsequent log records are counted and not blocked.
 */
class test_sink_backend :
    public sinks::basic_sink_backend< sinks::combine_requirements<
        sinks::synchronized_feeding,
        sinks::flushing
    >::type >
{
private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    //! Number of consume calls that have entered the method
    unsigned int m_consume_calls;
    //! Whether the blocked consume call must throw
    bool m_release;
    //! Number of log records received without throwing
    unsigned int m_fed_records;

public:
    test_sink_backend() : m_consume_calls(0u), m_release(false), m_fed_records(0u)
    {
    }

    //! Blocks until a log record has entered the backend
    void wait_in_consume()
    {
        std::unique_lock< std::mutex > lock(m_mutex);
        m_cond.wait(lock, [this] { return m_consume_calls != 0u; });
    }

    //! Unblocks the blocked consume call, making it throw
    void release_consume()
    {
        {
            std::lock_guard< std::mutex > lock(m_mutex);
            m_release = true;
        }
        m_cond.notify_all();
    }

    //! Returns the number of log records received without throwing
    unsigned int get_fed_record_count() const
    {
        std::lock_guard< std::mutex > lock(m_mutex);
        return m_fed_records;
    }

    //! The method receives the log message
    void consume(logging::record_view const&)
    {
        std::unique_lock< std::mutex > lock(m_mutex);
        ++m_consume_calls;

        if (m_consume_calls == 1u)
        {
            m_cond.notify_all();
            m_cond.wait(lock, [this] { return m_release; });
            throw std::runtime_error("Simulated failure in the sink backend");
        }

        ++m_fed_records;
    }

    //! The method implements flushing any buffers
    void flush() {}
};

BOOST_AUTO_TEST_CASE(sink_async_unbounded_fifo_flush_feeding_exception)
{
    typedef sinks::asynchronous_sink< test_sink_backend, sinks::unbounded_fifo_queue > sink_type;

    auto backend = boost::make_shared< test_sink_backend >();
    // The sink is constructed without a dedicated feeding thread so that the test can run the feeding loop itself
    auto sink = boost::make_shared< sink_type >(backend, false);

    std::atomic< bool > feeder_threw(false);
    test_thread feeder([&]()
    {
        try
        {
            sink->run();
        }
        catch (std::runtime_error const&)
        {
            feeder_threw.store(true, std::memory_order_relaxed);
        }
    });

    // Enqueue a log record that will block the feeding operation inside the backend
    sink->consume(make_record_view());
    backend->wait_in_consume();

    // Request a flush. Since a record feeding operation is active, the caller delegates the flush to it
    test_thread flusher([&]()
    {
        sink->flush();
    });

    // Wait until the flush request becomes active. `try_consume` rejects log records while a flush
    // is in progress, which is observable through its return value because the queue is unbounded.
    // The flush request cannot complete before it is observed (completing requires releasing the
    // backend below), so this loop is guaranteed to exit once the flusher registered the request.
    unsigned int buffered_records = 0u;
    while (sink->try_consume(make_record_view()))
    {
        ++buffered_records;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Unblock the backend, making the feeding operation fail with an exception
    // before it gets a chance to perform the requested flush
    backend->release_consume();

    // The flusher must not remain blocked and must complete the flush synchronously
    const auto deadline = std::chrono::steady_clock::now() + join_timeout;

    bool flush_completed = flusher.try_join_until(deadline);
    BOOST_CHECK(flush_completed);

    BOOST_CHECK(feeder.try_join_until(deadline));
    BOOST_CHECK(feeder_threw.load(std::memory_order_relaxed));

    if (flush_completed)
    {
        // The records that were buffered when the flush was requested must have been fed to the backend
        BOOST_CHECK_EQUAL(backend->get_fed_record_count(), buffered_records);
    }
    else
    {
        // The flusher thread is stuck in the sink. Detach it and intentionally leak a reference to the sink
        // to avoid destroying it while the thread is still blocked in it.
        flusher.detach();
        new boost::shared_ptr< sink_type >(sink);
    }
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
