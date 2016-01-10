/*
 *               Copyright Lingxi Li 2015.
 *            Copyright Andrey Semashev 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   util_ipc_reliable_mq.cpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   19.10.2015
 *
 * \brief  The test verifies that \c ipc::reliable_message_queue works.
 */

#define BOOST_CHECK_MODULE util_ipc_reliable_mq

#include <boost/log/utility/ipc/reliable_message_queue.hpp>
#include <boost/log/utility/open_mode.hpp>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstring>
#include <string>
#include <iostream>
#include <stdexcept>
#include <boost/move/utility.hpp>
#include "char_definitions.hpp"

const char ipc_queue_name[] = "boost_log_test_ipc_reliable_mq";

BOOST_AUTO_TEST_CASE(message_queue)
{
    typedef boost::log::ipc::reliable_message_queue queue_t;

    const unsigned int capacity = 2048;
    const unsigned int block_size = 1024;

    // Default constructor.
    {
        queue_t queue;
        BOOST_CHECK(!queue.is_open());
    }

    // Do a remove in case if a previous test failed
    queue_t::remove(ipc_queue_name);

    // Opening a non-existing queue
    try
    {
        queue_t queue(boost::log::open_mode::open_only, ipc_queue_name);
        BOOST_FAIL("Non-existing queue open succeeded, although it shouldn't have");
    }
    catch (std::exception&)
    {
        BOOST_TEST_PASSPOINT();
    }

    // Create constructor and destructor.
    {
        queue_t queue(boost::log::open_mode::create_only, ipc_queue_name, capacity, block_size);
        BOOST_CHECK(equal_strings(queue.name(), ipc_queue_name));
        BOOST_CHECK(queue.is_open());
        BOOST_CHECK(queue.capacity() == capacity);
        BOOST_CHECK(queue.block_size() == block_size);
    }
    // Move constructor.
    {
        queue_t queue_a(boost::log::open_mode::create_only, ipc_queue_name, capacity, block_size);
        queue_t queue_b(boost::move(queue_a));
        BOOST_CHECK(!queue_a.is_open());
        BOOST_CHECK(equal_strings(queue_b.name(), ipc_queue_name));
        BOOST_CHECK(queue_b.is_open());
        BOOST_CHECK(queue_b.capacity() == capacity);
        BOOST_CHECK(queue_b.block_size() == block_size);
    }
    // Move assignment operator.
    {
        queue_t queue_a(boost::log::open_mode::create_only, ipc_queue_name, capacity, block_size);
        queue_t queue_b;
        queue_b = boost::move(queue_a);
        BOOST_CHECK(!queue_a.is_open());
        BOOST_CHECK(equal_strings(queue_b.name(), ipc_queue_name));
        BOOST_CHECK(queue_b.is_open());
        BOOST_CHECK(queue_b.capacity() == capacity);
        BOOST_CHECK(queue_b.block_size() == block_size);
    }
    // Member and non-member swaps.
    {
        queue_t queue_a(ipc_queue_name), queue_b;
        queue_a.swap(queue_a);
        BOOST_CHECK(queue_a.name() == ipc_queue_name);
        BOOST_CHECK(queue_a.is_open());
        BOOST_CHECK(queue_a.max_queue_size() == 10);
        BOOST_CHECK(queue_a.max_message_size() == 1000);
        swap(queue_a, queue_b);
        BOOST_CHECK(queue_a.name().empty());
        BOOST_CHECK(!queue_a.is_open());
        BOOST_CHECK(queue_b.name() == ipc_queue_name);
        BOOST_CHECK(queue_b.is_open());
        BOOST_CHECK(queue_b.max_queue_size() == 10);
        BOOST_CHECK(queue_b.max_message_size() == 1000);
    }
    // open().
    {
        // open() performs close() first if a message queue is currently associated.
        queue_t queue_d(ipc_queue_name);
        BOOST_CHECK(!queue_d.open(ipc_queue_name, queue_d.open_only));
        BOOST_CHECK(queue_d.name().empty());
        BOOST_CHECK(!queue_d.is_open());
        BOOST_CHECK(errno == ENOENT);
        // Trivial case.
        queue_t queue(ipc_queue_name);
        BOOST_CHECK(queue.open(ipc_queue_name));
        BOOST_CHECK(queue.name() == ipc_queue_name);
        BOOST_CHECK(queue.is_open());
        BOOST_CHECK(queue.max_queue_size() == 10);
        BOOST_CHECK(queue.max_message_size() == 1000);
        BOOST_CHECK(errno == ENOENT);
        // Close semantics.
        BOOST_CHECK(queue.open(""));
        BOOST_CHECK(queue.name().empty());
        BOOST_CHECK(!queue.is_open());
        BOOST_CHECK(errno == 0);
        // create_only
        BOOST_CHECK(queue.open(ipc_queue_name, queue.create_only, 1, 2));
        BOOST_CHECK(queue.name() == ipc_queue_name);
        BOOST_CHECK(queue.is_open());
        BOOST_CHECK(queue.max_queue_size() == 1);
        BOOST_CHECK(queue.max_message_size() == 2);
        BOOST_CHECK(errno == ENOENT);
        // open_or_create
        queue_t queue_b;
        BOOST_CHECK(queue_b.open(ipc_queue_name));
        BOOST_CHECK(queue_b.name() == ipc_queue_name);
        BOOST_CHECK(queue_b.is_open());
        BOOST_CHECK(queue_b.max_queue_size() == 1);
        BOOST_CHECK(queue_b.max_message_size() == 2);
        BOOST_CHECK(errno == EEXIST);
        // open_only
        queue_t queue_c;
        BOOST_CHECK(queue_c.open(ipc_queue_name, queue_c.open_only));
        BOOST_CHECK(queue_c.name() == ipc_queue_name);
        BOOST_CHECK(queue_c.is_open());
        BOOST_CHECK(queue_c.max_queue_size() == 1);
        BOOST_CHECK(queue_c.max_message_size() == 2);
        BOOST_CHECK(errno == EEXIST);
        // Failure case.
        BOOST_CHECK(!queue_c.open("x_queue", queue_c.open_only));
        BOOST_CHECK(queue_c.name().empty());
        BOOST_CHECK(!queue_c.is_open());
        BOOST_CHECK(errno == ENOENT);
    }
    // is_open(). Done already.
    // clear()
    {
        queue_t queue(ipc_queue_name, queue_t::create_only, 1, 1);
        BOOST_CHECK(queue.is_open());
        queue_t queue2(ipc_queue_name, queue_t::open_only);
        BOOST_CHECK(queue2.is_open());
        BOOST_CHECK(queue.try_send("x", 1));
        char c = '\0';
        unsigned message_size = 0;
        BOOST_CHECK(queue2.try_receive(&c, 1, message_size));
        BOOST_CHECK(c == 'x');
        BOOST_CHECK(queue.try_send("x", 1));
        queue2.clear();
        BOOST_CHECK(!queue2.try_receive(&c, 1, message_size));
    }
    // name(). Done already.
    // max_queue_size(). Done already.
    // max_message_size(). Done already.
    // stop() & reset()
    {
        queue_t queue(ipc_queue_name, queue_t::open_or_create, 1, 5);
        queue.reset();
        queue.stop();
        // The object now never blocks.
        BOOST_CHECK(queue.send("msg1", 4));
        BOOST_CHECK(!queue.try_send("msg2", 4));
        BOOST_CHECK(!queue.send("msg2", 4));
        BOOST_CHECK(errno == EINTR);
        char buffer[5] = {};
        unsigned int message_size = 0;
        BOOST_CHECK(queue.receive(buffer, 5, message_size));
        BOOST_CHECK(!queue.try_receive(buffer, 5, message_size));
        BOOST_CHECK(!queue.receive(buffer, 5, message_size));
        BOOST_CHECK(errno == EINTR);
    }
    // close().
    {
        queue_t queue(ipc_queue_name);
        queue.close();
        BOOST_CHECK(queue.name().empty());
        BOOST_CHECK(!queue.is_open());
        queue.close();
        BOOST_CHECK(queue.name().empty());
        BOOST_CHECK(!queue.is_open());
    }
    // send() and receive().
    {
        queue_t queue(ipc_queue_name, queue_t::create_only, 1, 3);
        BOOST_CHECK(errno == ENOENT);
        BOOST_CHECK(queue.send("123", 3));
        char buffer[4] = {};
        unsigned int message_size = 0;
        BOOST_CHECK(queue.receive(buffer, 3, message_size));
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
    }
    // try_send() and try_receive()
    {
        queue_t queue(ipc_queue_name, queue_t::create_only, 1, 3);
        BOOST_CHECK(queue.try_send("123", 3));
        BOOST_CHECK(!queue.try_send("456", 3));
        char buffer[4] = {};
        unsigned int message_size = 0;
        BOOST_CHECK(queue.try_receive(buffer, 3, message_size));
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
        BOOST_CHECK(!queue.try_receive(buffer, 3, message_size));
    }
}
