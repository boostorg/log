/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_ipc_backend.cpp
 * \author Lingxi Li
 * \date   19.10.2015
 *
 * \brief  The test aims to fully exercise \c text_ipc_message_queue_backend and the
 *         supporting \c message_queue_type and to ensure that everything works as expected.
 */

#define BOOST_TEST_MODULE sink_ipc_backend

#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>
#include <boost/test/unit_test.hpp>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <boost/move/utility.hpp>

using namespace boost::log::keywords;
using namespace boost::log::sinks;

typedef text_ipc_message_queue_backend backend_t;
typedef backend_t::message_queue_type  queue_t;
typedef queue_t::permission            permission_t;

namespace boost {
BOOST_LOG_OPEN_NAMESPACE
namespace sinks {

//! Permission implementation data
template < typename CharT >
struct basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::implementation
{
#ifdef BOOST_WINDOWS
    shared_ptr< SECURITY_ATTRIBUTES > m_pSecurityAttr;
#else
    mode_t m_Permission;
#endif
};

} // namespace sinks
BOOST_LOG_CLOSE_NAMESPACE
} // namespace boost

#ifdef BOOST_WINDOWS

SECURITY_ATTRIBUTES* get_native_value(permission_t const& perm)
{
    return perm.m_pImpl->m_pSecurityAttr.get();
}

// The test checks that `permission` works on Windows platforms.
BOOST_AUTO_TEST_CASE(permission_test)
{
    permission_t perm_a;
    BOOST_TEST(!get_native_value(perm_a));
    boost::shared_ptr< SECURITY_ATTRIBUTES > ptr(new SECURITY_ATTRIBUTES());
    permission_t perm_b(ptr);
    BOOST_TEST(get_native_value(perm_b) == ptr.get());
    permission_t perm_c(perm_b);
    BOOST_TEST(get_native_value(perm_c) == ptr.get());
    permission_t perm_d(boost::move(perm_c));
    BOOST_TEST(get_native_value(perm_d) == ptr.get());
    BOOST_TEST(!get_native_value(perm_c));
    BOOST_TEST(&(perm_c = perm_d) == &perm_c);
    BOOST_TEST(get_native_value(perm_c) == ptr.get());
    BOOST_TEST(&(perm_c = perm_c) == &perm_c);
    BOOST_TEST(get_native_value(perm_c) == ptr.get());
    BOOST_TEST(&(perm_d = boost::move(perm_c)) == &perm_d);
    BOOST_TEST(get_native_value(perm_d) == ptr.get());
    BOOST_TEST(!get_native_value(perm_c));
    BOOST_TEST(&(perm_d = boost::move(perm_d)) == &perm_d);
    BOOST_TEST(get_native_value(perm_d) == ptr.get());
    perm_c.swap(perm_c);
    BOOST_TEST(!get_native_value(perm_c));
    swap(perm_c, perm_c);
    BOOST_TEST(!get_native_value(perm_c));
    perm_c.swap(perm_d);
    BOOST_TEST(!get_native_value(perm_d));
    BOOST_TEST(get_native_value(perm_c) == ptr.get());
    swap(perm_c, perm_d);
    BOOST_TEST(get_native_value(perm_d) == ptr.get());
    BOOST_TEST(!get_native_value(perm_c));
}

#else // POSIX

mode_t get_native_value(permission_t const& perm)
{
    return perm.m_pImpl->m_Permission;
}

// The test checks that `permission` works on POSIX platforms.
BOOST_AUTO_TEST_CASE(permission_test)
{
    permission_t perm_a;
    BOOST_TEST(get_native_value(perm_a) == 0644);
    permission_t perm_b(0666);
    BOOST_TEST(get_native_value(perm_b) == 0666);
    permission_t perm_c(perm_b);
    BOOST_TEST(get_native_value(perm_c) == 0666);
    permission_t perm_d(boost::move(perm_c));
    BOOST_TEST(get_native_value(perm_d) == 0666);
    BOOST_TEST(get_native_value(perm_c) == 0644);
    BOOST_TEST(&(perm_c = perm_d) == &perm_c);
    BOOST_TEST(get_native_value(perm_c) == 0666);
    BOOST_TEST(&(perm_c = perm_c) == &perm_c);
    BOOST_TEST(get_native_value(perm_c) == 0666);
    BOOST_TEST(&(perm_d = boost::move(perm_c)) == &perm_d);
    BOOST_TEST(get_native_value(perm_d) == 0666);
    BOOST_TEST(get_native_value(perm_c) == 0644);
    BOOST_TEST(&(perm_d = boost::move(perm_d)) == &perm_d);
    BOOST_TEST(get_native_value(perm_d) == 0666);
    perm_c.swap(perm_c);
    BOOST_TEST(get_native_value(perm_c) == 0644);
    swap(perm_c, perm_c);
    BOOST_TEST(get_native_value(perm_c) == 0644);
    perm_c.swap(perm_d);
    BOOST_TEST(get_native_value(perm_d) == 0644);
    BOOST_TEST(get_native_value(perm_c) == 0666);
    swap(perm_c, perm_d);
    BOOST_TEST(get_native_value(perm_d) == 0666);
    BOOST_TEST(get_native_value(perm_c) == 0644);
}

#endif // BOOST_WINDOWS

// The test checks that `message_queue_type` works.
BOOST_AUTO_TEST_CASE(message_queue)
{
    // Default constructor.
    {
        queue_t queue;
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
    }
    // Open constructor and destructor.
    {
        queue_t queue("queue");
        BOOST_TEST(queue.name() == "queue");
        BOOST_TEST(queue.is_open());
        BOOST_TEST(queue.max_queue_size() == 10);
        BOOST_TEST(queue.max_message_size() == 1000);
        BOOST_TEST(errno == ENOENT);
    }
    // Move constructor.
    {
        queue_t queue_a("queue");
        queue_t queue_b(boost::move(queue_a));
        BOOST_TEST(queue_a.name().empty());
        BOOST_TEST(!queue_a.is_open());
        BOOST_TEST(queue_b.name() == "queue");
        BOOST_TEST(queue_b.is_open());
        BOOST_TEST(queue_b.max_queue_size() == 10);
        BOOST_TEST(queue_b.max_message_size() == 1000);
    }
    // Move assignment operator.
    {
        queue_t queue_a("queue");
        BOOST_TEST(&(queue_a = boost::move(queue_a)) == &queue_a);
        BOOST_TEST(queue_a.name() == "queue");
        BOOST_TEST(queue_a.is_open());
        BOOST_TEST(queue_a.max_queue_size() == 10);
        BOOST_TEST(queue_a.max_message_size() == 1000);

        queue_t queue_b;
        BOOST_TEST(&(queue_b = boost::move(queue_a)) == &queue_b);
        BOOST_TEST(queue_b.name() == "queue");
        BOOST_TEST(queue_b.is_open());
        BOOST_TEST(queue_b.max_queue_size() == 10);
        BOOST_TEST(queue_b.max_message_size() == 1000);
        BOOST_TEST(queue_a.name().empty());
        BOOST_TEST(!queue_a.is_open());
    }
    // Member and non-member swaps.
    {
        queue_t queue_a("queue"), queue_b;
        queue_a.swap(queue_a);
        BOOST_TEST(queue_a.name() == "queue");
        BOOST_TEST(queue_a.is_open());
        BOOST_TEST(queue_a.max_queue_size() == 10);
        BOOST_TEST(queue_a.max_message_size() == 1000);
        swap(queue_a, queue_b);
        BOOST_TEST(queue_a.name().empty());
        BOOST_TEST(!queue_a.is_open());
        BOOST_TEST(queue_b.name() == "queue");
        BOOST_TEST(queue_b.is_open());
        BOOST_TEST(queue_b.max_queue_size() == 10);
        BOOST_TEST(queue_b.max_message_size() == 1000);
    }
    // open().
    {
        // open() performs close() first if a message queue is currently associated.
        queue_t queue_d("queue");
        BOOST_TEST(!queue_d.open("queue", queue_d.open_only));
        BOOST_TEST(queue_d.name().empty());
        BOOST_TEST(!queue_d.is_open());
        BOOST_TEST(errno == ENOENT);
        // Trivial case.
        queue_t queue("queue");
        BOOST_TEST(queue.open("queue"));
        BOOST_TEST(queue.name() == "queue");
        BOOST_TEST(queue.is_open());
        BOOST_TEST(queue.max_queue_size() == 10);
        BOOST_TEST(queue.max_message_size() == 1000);
        BOOST_TEST(errno == ENOENT);
        // Close semantics.
        BOOST_TEST(queue.open(""));
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
        BOOST_TEST(errno == 0);
        // create_only
        BOOST_TEST(queue.open("queue", queue.create_only, 1, 2));
        BOOST_TEST(queue.name() == "queue");
        BOOST_TEST(queue.is_open());
        BOOST_TEST(queue.max_queue_size() == 1);
        BOOST_TEST(queue.max_message_size() == 2);
        BOOST_TEST(errno == ENOENT);
        // open_or_create
        queue_t queue_b;
        BOOST_TEST(queue_b.open("queue"));
        BOOST_TEST(queue_b.name() == "queue");
        BOOST_TEST(queue_b.is_open());
        BOOST_TEST(queue_b.max_queue_size() == 1);
        BOOST_TEST(queue_b.max_message_size() == 2);
        BOOST_TEST(errno == EEXIST);
        // open_only
        queue_t queue_c;
        BOOST_TEST(queue_c.open("queue", queue_c.open_only));
        BOOST_TEST(queue_c.name() == "queue");
        BOOST_TEST(queue_c.is_open());
        BOOST_TEST(queue_c.max_queue_size() == 1);
        BOOST_TEST(queue_c.max_message_size() == 2);
        BOOST_TEST(errno == EEXIST);
        // Failure case.
        BOOST_TEST(!queue_c.open("x_queue", queue_c.open_only));
        BOOST_TEST(queue_c.name().empty());
        BOOST_TEST(!queue_c.is_open());
        BOOST_TEST(errno == ENOENT);
    }
    // is_open(). Done already.
    // clear()
    {
        queue_t queue("queue", queue_t::create_only, 1, 1);
        BOOST_TEST(queue.is_open());
        queue_t queue2("queue", queue_t::open_only);
        BOOST_TEST(queue2.is_open());
        BOOST_TEST(queue.try_send("x", 1));
        char c = '\0';
        unsigned message_size = 0;
        BOOST_TEST(queue2.try_receive(&c, 1, message_size));
        BOOST_TEST(c == 'x');
        BOOST_TEST(queue.try_send("x", 1));
        queue2.clear();
        BOOST_TEST(!queue2.try_receive(&c, 1, message_size));
    }
    // name(). Done already.
    // max_queue_size(). Done already.
    // max_message_size(). Done already.
    // stop() & reset()
    {
        queue_t queue("queue", queue_t::open_or_create, 1, 5);
        queue.reset();
        queue.stop();
        // The object now never blocks.
        BOOST_TEST(queue.send("msg1", 4));
        BOOST_TEST(!queue.try_send("msg2", 4));
        BOOST_TEST(!queue.send("msg2", 4));
        BOOST_TEST(errno == EINTR);
        char buffer[5] = {};
        unsigned int message_size = 0;
        BOOST_TEST(queue.receive(buffer, 5, message_size));
        BOOST_TEST(!queue.try_receive(buffer, 5, message_size));
        BOOST_TEST(!queue.receive(buffer, 5, message_size));
        BOOST_TEST(errno == EINTR);
    }
    // close().
    {
        queue_t queue("queue");
        queue.close();
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
        queue.close();
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
    }
    // send() and receive().
    {
        queue_t queue("queue", queue_t::create_only, 1, 3);
        BOOST_TEST(errno == ENOENT);
        BOOST_TEST(queue.send("123", 3));
        char buffer[4] = {};
        unsigned int message_size = 0;
        BOOST_TEST(queue.receive(buffer, 3, message_size));
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
    }
    // try_send() and try_receive()
    {
        queue_t queue("queue", queue_t::create_only, 1, 3);
        BOOST_TEST(queue.try_send("123", 3));
        BOOST_TEST(!queue.try_send("456", 3));
        char buffer[4] = {};
        unsigned int message_size = 0;
        BOOST_TEST(queue.try_receive(buffer, 3, message_size));
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
        BOOST_TEST(!queue.try_receive(buffer, 3, message_size));
    }
}

// The test checks that `text_ipc_message_queue_backend` works.
BOOST_AUTO_TEST_CASE(ipc_backend)
{
    // Default constructor.
    {
        backend_t backend;
        BOOST_TEST(backend.name().empty());
        BOOST_TEST(!backend.is_open());
        BOOST_TEST(backend.queue_policy() == backend.drop_when_full);
        BOOST_TEST(backend.message_policy() == backend.throw_when_too_long);
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
        BOOST_TEST(errno == ENOENT);
        BOOST_TEST(backend.name() == "queue");
        BOOST_TEST(backend.is_open());
        BOOST_TEST(backend.max_queue_size() == 1);
        BOOST_TEST(backend.max_message_size() == 2);
        BOOST_TEST(backend.queue_policy() == backend.block_when_full);
        BOOST_TEST(backend.message_policy() == backend.truncate_when_too_long);
    }
    // message_queue().
    {
        backend_t non_const_backend;
        BOOST_TEST(&non_const_backend.message_queue());
        backend_t const const_backend;
        BOOST_TEST(&const_backend.message_queue());
    }
    // name(). Done already.
    // open().
    {
        // open() performs close() first if a message queue is currently associated.
        backend_t queue_d(message_queue_name = "queue");
        BOOST_TEST(!queue_d.open("queue", queue_d.open_only));
        BOOST_TEST(queue_d.name().empty());
        BOOST_TEST(!queue_d.is_open());
        BOOST_TEST(errno == ENOENT);
        // Trivial case.
        // The backend type has a default open mode which is different from that
        // of the underlying message queue type.
        backend_t queue(message_queue_name = "queue");
        BOOST_TEST(!queue.open("queue"));
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
        BOOST_TEST(errno == ENOENT);
        // Close semantics.
        BOOST_TEST(queue.open(""));
        BOOST_TEST(queue.name().empty());
        BOOST_TEST(!queue.is_open());
        BOOST_TEST(errno == 0);
        // create_only
        BOOST_TEST(queue.open("queue", queue.create_only, 1, 2));
        BOOST_TEST(queue.name() == "queue");
        BOOST_TEST(queue.is_open());
        BOOST_TEST(queue.max_queue_size() == 1);
        BOOST_TEST(queue.max_message_size() == 2);
        BOOST_TEST(errno == ENOENT);
        // open_or_create
        backend_t queue_b;
        BOOST_TEST(queue_b.open("queue"));
        BOOST_TEST(queue_b.name() == "queue");
        BOOST_TEST(queue_b.is_open());
        BOOST_TEST(queue_b.max_queue_size() == 1);
        BOOST_TEST(queue_b.max_message_size() == 2);
        BOOST_TEST(errno == EEXIST);
        // open_only
        backend_t queue_c;
        BOOST_TEST(queue_c.open("queue", queue_c.open_only));
        BOOST_TEST(queue_c.name() == "queue");
        BOOST_TEST(queue_c.is_open());
        BOOST_TEST(queue_c.max_queue_size() == 1);
        BOOST_TEST(queue_c.max_message_size() == 2);
        BOOST_TEST(errno == EEXIST);
        // Failure case.
        BOOST_TEST(!queue_c.open("x_queue", queue_c.open_only));
        BOOST_TEST(queue_c.name().empty());
        BOOST_TEST(!queue_c.is_open());
        BOOST_TEST(errno == ENOENT);
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
        BOOST_TEST(errno == EINTR);
    }
    // close().
    {
        backend_t backend(
            message_queue_name = "queue",
            open_mode = backend_t::open_or_create);
        backend.close();
        BOOST_TEST(backend.name().empty());
        BOOST_TEST(!backend.is_open());
        backend.close();
        BOOST_TEST(backend.name().empty());
        BOOST_TEST(!backend.is_open());
    }
    // set_queue_policy().
    {
        backend_t backend;
        backend.set_queue_policy(backend.block_when_full);
        BOOST_TEST(backend.queue_policy() == backend.block_when_full);
    }
    // set_message_policy().
    {
        backend_t backend;
        backend.set_message_policy(backend.truncate_when_too_long);
        BOOST_TEST(backend.message_policy() == backend.truncate_when_too_long);
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
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
        // Drop when full.
        backend.consume(rec, "123");
        backend.consume(rec, "456");
        queue.try_receive(buffer, 3, message_size);
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
        BOOST_TEST(!queue.try_receive(buffer, 3, message_size));
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
            BOOST_TEST(std::strcmp(e.what(), "Message queue is full.") == 0);
            thrown = true;
        }
        BOOST_TEST(thrown);
        queue.try_receive(buffer, 3, message_size);
        // Block when full.
        backend.set_queue_policy(backend.block_when_full);
        backend.consume(rec, "123");
        queue.try_receive(buffer, 3, message_size);
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
        // Truncate when too long.
        backend.set_message_policy(backend.truncate_when_too_long);
        backend.consume(rec, "12345");
        queue.try_receive(buffer, 3, message_size);
        BOOST_TEST(std::strcmp(buffer, "123") == 0);
        BOOST_TEST(message_size == 3);
        // Drop when too long.
        backend.set_message_policy(backend.drop_when_too_long);
        backend.consume(rec, "12345");
        BOOST_TEST(!queue.try_receive(buffer, 3, message_size));
        // Throw when too long.
        backend.set_message_policy(backend.throw_when_too_long);
        thrown = false;
        try
        {
            backend.consume(rec, "12345");
        }
        catch (std::logic_error const& e)
        {
            BOOST_TEST(std::strcmp(e.what(), "Message is too long.") == 0);
            thrown = true;
        }
        BOOST_TEST(thrown);
    }
}
