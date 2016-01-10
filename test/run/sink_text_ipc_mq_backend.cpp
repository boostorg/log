/*
 *                Copyright Lingxi Li 2015.
 *             Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_text_ipc_mq_backend.cpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   19.10.2015
 *
 * \brief  The test verifies that \c text_ipc_message_queue_backend works as expected.
 */

#define BOOST_TEST_MODULE sink_text_ipc_mq_backend

#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>
#include <boost/log/utility/ipc/reliable_message_queue.hpp>
#include <boost/log/utility/open_mode.hpp>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <boost/move/utility.hpp>

namespace keywords = boost::log::keywords;

const char ipc_queue_name[] = "boost_log_test_text_ipc_mq_backend";
const unsigned int capacity = 2048;
const unsigned int block_size = 1024;

// The test checks that `text_ipc_message_queue_backend` works.
BOOST_AUTO_TEST_CASE(text_ipc_message_queue_backend)
{
    typedef boost::log::sinks::text_ipc_message_queue_backend< boost::log::ipc::reliable_message_queue > backend_t;

    // Do a remove in case if a previous test failed
    boost::log::ipc::reliable_message_queue::remove(ipc_queue_name);

    // Default constructor.
    {
        backend_t backend;
        BOOST_CHECK(backend.name().empty());
        BOOST_CHECK(!backend.is_open());
        BOOST_CHECK(backend.queue_policy() == backend.drop_when_full);
        BOOST_CHECK(backend.message_policy() == backend.throw_when_too_long);
    }
    // Open constructor and destructor.
    {
        backend_t backend(
            message_queue_name = "queue",
            open_mode = backend_t::create_only,
            max_queue_size = 1,
            max_message_size = 2,
            queue_policy = backend_t::block_when_full,
            message_policy = backend_t::truncate_when_too_long,
            permission = permission_t());
        BOOST_CHECK(errno == ENOENT);
        BOOST_CHECK(backend.name() == "queue");
        BOOST_CHECK(backend.is_open());
        BOOST_CHECK(backend.max_queue_size() == 1);
        BOOST_CHECK(backend.max_message_size() == 2);
        BOOST_CHECK(backend.queue_policy() == backend.block_when_full);
        BOOST_CHECK(backend.message_policy() == backend.truncate_when_too_long);
    }
    // message_queue().
    {
        backend_t non_const_backend;
        BOOST_CHECK(&non_const_backend.message_queue());
        backend_t const const_backend;
        BOOST_CHECK(&const_backend.message_queue());
    }
    // name(). Done already.
    // open().
    {
        // open() performs close() first if a message queue is currently associated.
        backend_t queue_d(message_queue_name = "queue");
        BOOST_CHECK(!queue_d.open("queue", queue_d.open_only));
        BOOST_CHECK(queue_d.name().empty());
        BOOST_CHECK(!queue_d.is_open());
        BOOST_CHECK(errno == ENOENT);
        // Trivial case.
        // The backend type has a default open mode which is different from that
        // of the underlying message queue type.
        backend_t queue(message_queue_name = "queue");
        BOOST_CHECK(!queue.open("queue"));
        BOOST_CHECK(queue.name().empty());
        BOOST_CHECK(!queue.is_open());
        BOOST_CHECK(errno == ENOENT);
        // Close semantics.
        BOOST_CHECK(queue.open(""));
        BOOST_CHECK(queue.name().empty());
        BOOST_CHECK(!queue.is_open());
        BOOST_CHECK(errno == 0);
        // create_only
        BOOST_CHECK(queue.open("queue", queue.create_only, 1, 2));
        BOOST_CHECK(queue.name() == "queue");
        BOOST_CHECK(queue.is_open());
        BOOST_CHECK(queue.max_queue_size() == 1);
        BOOST_CHECK(queue.max_message_size() == 2);
        BOOST_CHECK(errno == ENOENT);
        // open_or_create
        backend_t queue_b;
        BOOST_CHECK(queue_b.open("queue"));
        BOOST_CHECK(queue_b.name() == "queue");
        BOOST_CHECK(queue_b.is_open());
        BOOST_CHECK(queue_b.max_queue_size() == 1);
        BOOST_CHECK(queue_b.max_message_size() == 2);
        BOOST_CHECK(errno == EEXIST);
        // open_only
        backend_t queue_c;
        BOOST_CHECK(queue_c.open("queue", queue_c.open_only));
        BOOST_CHECK(queue_c.name() == "queue");
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
    // max_queue_size(). Done already.
    // max_message_size(). Done already.
    // stop() and reset().
    {
        backend_t queue(
            message_queue_name = "queue",
            open_mode = queue_t::open_or_create,
            max_queue_size = 1,
            queue_policy = backend_t::block_when_full);
        queue.reset();
        queue.stop();
        boost::log::record_view rec;
        // The object now never blocks.
        queue.consume(rec, "msg1");
        queue.consume(rec, "msg2");
        BOOST_CHECK(errno == EINTR);
    }
    // close().
    {
        backend_t backend(
            message_queue_name = "queue",
            open_mode = backend_t::open_or_create);
        backend.close();
        BOOST_CHECK(backend.name().empty());
        BOOST_CHECK(!backend.is_open());
        backend.close();
        BOOST_CHECK(backend.name().empty());
        BOOST_CHECK(!backend.is_open());
    }
    // set_queue_policy().
    {
        backend_t backend;
        backend.set_queue_policy(backend.block_when_full);
        BOOST_CHECK(backend.queue_policy() == backend.block_when_full);
    }
    // set_message_policy().
    {
        backend_t backend;
        backend.set_message_policy(backend.truncate_when_too_long);
        BOOST_CHECK(backend.message_policy() == backend.truncate_when_too_long);
    }
    // queue_policy(). Done already.
    // message_policy(). Done already.
    // consume().
    {
        boost::log::record_view rec;
        backend_t backend;
        backend.consume(rec, "123");
        backend.open("queue", backend.create_only, 1, 3);
        queue_t queue("queue");
        char buffer[4] = {};
        // Normal case.
        backend.consume(rec, "123");
        unsigned int message_size = 0;
        queue.try_receive(buffer, 3, message_size);
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
        // Drop when full.
        backend.consume(rec, "123");
        backend.consume(rec, "456");
        queue.try_receive(buffer, 3, message_size);
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
        BOOST_CHECK(!queue.try_receive(buffer, 3, message_size));
        bool thrown = false;
        // Throw when full.
        backend.set_queue_policy(backend.throw_when_full);
        try
        {
            backend.consume(rec, "123");
            backend.consume(rec, "456");
        }
        catch (std::runtime_error const& e)
        {
            BOOST_CHECK(std::strcmp(e.what(), "Message queue is full.") == 0);
            thrown = true;
        }
        BOOST_CHECK(thrown);
        queue.try_receive(buffer, 3, message_size);
        // Block when full.
        backend.set_queue_policy(backend.block_when_full);
        backend.consume(rec, "123");
        queue.try_receive(buffer, 3, message_size);
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
        // Truncate when too long.
        backend.set_message_policy(backend.truncate_when_too_long);
        backend.consume(rec, "12345");
        queue.try_receive(buffer, 3, message_size);
        BOOST_CHECK(std::strcmp(buffer, "123") == 0);
        BOOST_CHECK(message_size == 3);
        // Drop when too long.
        backend.set_message_policy(backend.drop_when_too_long);
        backend.consume(rec, "12345");
        BOOST_CHECK(!queue.try_receive(buffer, 3, message_size));
        // Throw when too long.
        backend.set_message_policy(backend.throw_when_too_long);
        thrown = false;
        try
        {
            backend.consume(rec, "12345");
        }
        catch (std::logic_error const& e)
        {
            BOOST_CHECK(std::strcmp(e.what(), "Message is too long.") == 0);
            thrown = true;
        }
        BOOST_CHECK(thrown);
    }
}
