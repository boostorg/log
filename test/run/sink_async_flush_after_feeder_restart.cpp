/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE sink_async_flush_after_feeder_restart

#include <boost/log/core/core.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <boost/test/unit_test.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/unbounded_ordering_queue.hpp>

#include "test_async_sink.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

using namespace async_sink_test;

class restart_queue : public coordinated_queue< sinks::unbounded_ordering_queue< record_order > >
{
    using base_type = coordinated_queue< sinks::unbounded_ordering_queue< record_order > >;
    bool m_first_dequeue = true;

public:
    test_barrier* restarted = nullptr;

protected:
    template< typename ArgsT >
    explicit restart_queue(ArgsT const& args) : base_type(args) {}

    bool try_dequeue_ready(logging::record_view& rec)
    {
        if (m_first_dequeue)
        {
            m_first_dequeue = false;
            return base_type::try_dequeue(rec);
        }
        return base_type::try_dequeue_ready(rec);
    }

    bool dequeue_ready(logging::record_view& rec)
    {
        if (restarted)
        {
            auto* barrier = restarted;
            restarted = nullptr;
            barrier->arrive_and_wait();
        }
        return base_type::dequeue_ready(rec);
    }
};

BOOST_AUTO_TEST_CASE(sink_async_flush_after_feeder_restart)
{
    test_watchdog deadline;
    for (unsigned int i = 0u; i < 32u; ++i)
    {
        auto backend = boost::make_shared< controlled_backend >();
        sinks::asynchronous_sink< controlled_backend, restart_queue > sink(backend,
            logging::keywords::order = record_order(), logging::keywords::ordering_window = std::chrono::hours(1),
            logging::keywords::start_thread = false);
        test_barrier entered(2u), interrupted(2u), restarted(2u);
        backend->consume_entered = &entered;
        backend->consume_resume = &interrupted;
        backend->fail_consume = true;
        sink.interrupted = &interrupted;
        sink.restarted = &restarted;
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
            sink.run();
        });
        entered.arrive_and_wait();
        std::thread flusher([&]() { sink.flush(); });
        restarted.arrive_and_wait();
        flusher.join();
        BOOST_CHECK_EQUAL(backend->consumed.load(std::memory_order_relaxed), 1u);
        sink.stop();
        feeder.join();
    }
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
