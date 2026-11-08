/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_LOG_TEST_BOUNDED_QUEUE_HPP_INCLUDED_
#define BOOST_LOG_TEST_BOUNDED_QUEUE_HPP_INCLUDED_

#include <atomic>
#include <thread>
#include <boost/test/unit_test.hpp>
#include <boost/log/sinks/block_on_overflow.hpp>

#include "test_barrier.hpp"
#include "test_sink_backend.hpp"
#include "make_record.hpp"

namespace bounded_queue_test {

namespace logging = boost::log;
namespace sinks = boost::log::sinks;

template< typename QueueT >
class coordinated_bounded_queue : public QueueT
{
public:
    test_barrier* interrupted = nullptr;

protected:
    coordinated_bounded_queue() = default;

    template< typename ArgsT >
    explicit coordinated_bounded_queue(ArgsT const& args) : QueueT(args) {}

    void interrupt_dequeue()
    {
        QueueT::interrupt_dequeue();
        notify_interrupted();
    }

    void interrupt_dequeue_for_flush()
    {
        QueueT::interrupt_dequeue_for_flush();
        notify_interrupted();
    }

private:
    void notify_interrupted()
    {
        if (interrupted)
        {
            auto* barrier = interrupted;
            interrupted = nullptr;
            barrier->arrive_and_wait();
        }
    }
};

static test_barrier overflow_entered(2u);

class coordinated_overflow : public sinks::block_on_overflow
{
public:
    template< typename LockT >
    bool on_overflow(logging::record_view const& rec, LockT& lock)
    {
        overflow_entered.arrive_and_wait();
        return sinks::block_on_overflow::on_overflow(rec, lock);
    }
};

class controlled_backend : public test_backend
{
public:
    std::atomic< unsigned int > consumed{0u};
    test_barrier* consume_entered = nullptr;
    test_barrier* consume_resume = nullptr;

    void consume(logging::record_view const&)
    {
        if (consume_entered)
        {
            auto* entered = consume_entered;
            consume_entered = nullptr;
            entered->arrive_and_wait();
            consume_resume->arrive_and_wait();
        }
        consumed.fetch_add(1u, std::memory_order_relaxed);
    }
};

struct record_order
{
    using result_type = bool;
    bool operator()(logging::record_view const&, logging::record_view const&) const { return false; }
};

template< typename SinkT >
void check_flush_preserves_blocked_producer(SinkT& sink, controlled_backend& backend)
{
    test_barrier entered(2u), resume(2u), interrupted(2u);
    backend.consume_entered = &entered;
    backend.consume_resume = &resume;
    sink.interrupted = &interrupted;
    std::thread feeder([&]() { sink.run(); });

    sink.consume(make_record_view());
    entered.arrive_and_wait();
    sink.consume(make_record_view());
    std::thread producer([&]() { sink.consume(make_record_view()); });
    overflow_entered.arrive_and_wait();
    std::thread flusher([&]() { sink.flush(); });
    interrupted.arrive_and_wait();
    resume.arrive_and_wait();

    producer.join();
    flusher.join();
    sink.flush();
    sink.stop();
    feeder.join();
    BOOST_CHECK_EQUAL(backend.consumed.load(std::memory_order_relaxed), 3u);
}

} // namespace bounded_queue_test

#endif // BOOST_LOG_TEST_BOUNDED_QUEUE_HPP_INCLUDED_
