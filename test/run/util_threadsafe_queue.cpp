/*
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 */

#define BOOST_TEST_MODULE util_threadsafe_queue

#include <boost/log/detail/config.hpp>

#if !defined(BOOST_LOG_NO_THREADS)

#include <chrono>
#include <cstddef>
#include <thread>
#include <boost/test/unit_test.hpp>
#include <boost/log/detail/threadsafe_queue.hpp>

#include "test_barrier.hpp"

namespace {

struct payload
{
    std::size_t sequence;
    std::size_t checksum;
};

} // namespace

BOOST_AUTO_TEST_CASE(concurrent_publication_and_reclamation)
{
    const std::size_t record_count = 1024u;
    const std::size_t checksum_mask = 0x5a5a5a5au;
    boost::log::aux::threadsafe_queue< payload > queue;
    test_barrier barrier(2u);

    std::thread producer([&]()
    {
        barrier.arrive_and_wait();
        for (std::size_t i = 0u; i < record_count; ++i)
        {
            payload value = { i, i ^ checksum_mask };
            queue.push(value);
        }
    });

    // All payload writes follow the barrier; it cannot publish the queued values.
    barrier.arrive_and_wait();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::size_t consumed = 0u;
    bool valid = true;
    while (consumed < record_count)
    {
        payload value = {};
        if (queue.try_pop(value))
        {
            valid = valid && value.sequence == consumed && value.checksum == (consumed ^ checksum_mask);
            ++consumed;
        }
        else
        {
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            std::this_thread::yield();
        }
    }
    producer.join();

    BOOST_CHECK_EQUAL(consumed, record_count);
    BOOST_CHECK(valid);
}

#else // !defined(BOOST_LOG_NO_THREADS)

int main()
{
    return 0;
}

#endif // !defined(BOOST_LOG_NO_THREADS)
