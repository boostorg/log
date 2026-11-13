/*
 * Copyright Andrey Semashev 2026.
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef BOOST_LOG_TEST_WATCHDOG_HPP_INCLUDED_
#define BOOST_LOG_TEST_WATCHDOG_HPP_INCLUDED_

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

class test_watchdog
{
private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    bool m_finished = false;
    std::thread m_thread;

public:
    explicit test_watchdog(std::chrono::seconds timeout = std::chrono::seconds(10)) : m_thread([this, timeout]()
    {
        std::unique_lock< std::mutex > lock(m_mutex);
        if (!m_cond.wait_for(lock, timeout, [this]() { return m_finished; }))
        {
            std::fputs("Test exceeded its deadline\n", stderr);
            std::fflush(stderr);
            // Worker threads may still reference test objects, so do not unwind them.
            std::_Exit(EXIT_FAILURE);
        }
    })
    {
    }

    ~test_watchdog()
    {
        {
            std::lock_guard< std::mutex > lock(m_mutex);
            m_finished = true;
        }
        m_cond.notify_one();
        m_thread.join();
    }

    test_watchdog(test_watchdog const&) = delete;
    test_watchdog& operator=(test_watchdog const&) = delete;
};

#endif // BOOST_LOG_TEST_WATCHDOG_HPP_INCLUDED_
