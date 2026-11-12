/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_async_unbounded_fifo_order.cpp
 * \author Andrey Semashev
 * \date   08.11.2026
 *
 * \brief  This file contains a test for the log record order and memory ordering in `unbounded_fifo_queue`.
 *
 * The problem was reported in https://github.com/boostorg/log/pull/258.
 *
 * The `threadsafe_queue` that is used internally by the `unbounded_fifo_queue` policy did not enforce synchronizes-with
 * relation between the thread that published log records and the thread that consumed it, resulting in Thread Sanitizer
 * errors.
 *
 * The test below feeds a number of log records to a dummy sink using the `asynchronous_sink<unbounded_fifo_queue>`
 * frontend. The dummy sink consumes log records and checks their expected order. In order for the test to pass, the log
 * records must not be corrupted (which would trigger memory errors or incorrect content received by the backend) and
 * must come in the order they are published.
 */

#define BOOST_TEST_MODULE sink_async_unbounded_fifo_order

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <chrono>
#include <mutex>
#include <condition_variable>

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/core/record_view.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_fifo_queue.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;
namespace attrs = boost::log::attributes;

//! Number of log records to process
constexpr unsigned int log_record_count = 1000u;

//! Timeout for waiting the thread that processes log records
constexpr auto wait_timeout = std::chrono::seconds(5);

std::mutex g_mutex;
std::condition_variable g_condition_variable;
unsigned int g_error_count = 0u;
bool g_done = false;

/*!
 * \brief An implementation of a test logging sink backend
 *
 * The sink backend counts the number of received records and checks their order.
 */
class test_checking_backend :
    public sinks::basic_sink_backend< sinks::synchronized_feeding >
{
    //! Base type
    using base_type = sinks::basic_sink_backend< sinks::synchronized_feeding >;

private:
    unsigned int m_record_count;

public:
    test_checking_backend() : m_record_count(0u)
    {
    }

    //! The method receives the log message
    void consume(logging::record_view const& rec)
    {
        unsigned int value = rec.attribute_values()["Counter"].extract_or_default(static_cast< unsigned int >(-1));
        if (value != m_record_count)
        {
            std::lock_guard< std::mutex > lock(g_mutex);
            ++g_error_count;
        }

        ++m_record_count;
        if (m_record_count >= log_record_count)
        {
            std::lock_guard< std::mutex > lock(g_mutex);
            g_done = true;
            g_condition_variable.notify_one();
        }
    }
};

BOOST_AUTO_TEST_CASE(sink_async_unbounded_fifo_order)
{
    auto core = logging::core::get();
    core->add_sink(boost::make_shared< sinks::asynchronous_sink< test_checking_backend, sinks::unbounded_fifo_queue > >());

    {
        src::logger lg;

        for (unsigned int i = 0u; i < log_record_count; ++i)
        {
            auto attr(logging::add_scoped_logger_attribute(lg, "Counter", attrs::make_constant(i)));
            BOOST_LOG(lg) << "stress";
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + wait_timeout;

    std::unique_lock< std::mutex > lock(g_mutex);
    bool is_done = g_condition_variable.wait_until(lock, deadline, []() { return g_done; });
    BOOST_CHECK(is_done);
    BOOST_CHECK_EQUAL(g_error_count, 0u);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
