/*
 *                 Copyright Lingxi Li 2015.
 *              Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   text_ipc_message_queue_backend.hpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   14.10.2015
 *
 * The header contains implementation of a text interprocess message queue sink
 * backend along with implementation of a supporting interprocess message queue.
 */

#ifndef BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_
#define BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_

#include <string>
#include <boost/cstdint.hpp>
#include <boost/move/move.hpp>
#include <boost/throw_exception.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/keywords/message_queue_name.hpp>
#include <boost/log/keywords/open_mode.hpp>
#include <boost/log/keywords/max_message_size.hpp>
#include <boost/log/keywords/max_queue_size.hpp>
#include <boost/log/keywords/queue_policy.hpp>
#include <boost/log/keywords/message_policy.hpp>
#include <boost/log/keywords/permissions.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/detail/parameter_tools.hpp>
#include <boost/log/utility/permissions.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>
#include <boost/log/detail/header.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace sinks {

namespace ipc {

//! Interprocess queue overflow policies
enum queue_overflow_policy
{
    //! Drop the message when the queue is full
    drop_on_overflow,
    //! Throw an exception when the queue is full
    throw_on_overflow,
    //! Block the send operation when the queue is full
    block_on_overflow
};

} // namespace ipc

/*!
 * \brief An implementation of a text interprocess message queue sink backend and
 *        a supporting interprocess message queue.
 *
 * The sink backend sends formatted log messages to an interprocess message queue
 * which can be extracted by a viewer process. Methods of this class are not
 * thread-safe, unless otherwise specified.
 */
template< typename QueueT, ipc::queue_overflow_policy OverflowPolicyV = ipc::drop_on_overflow >
class text_ipc_message_queue_backend :
    public basic_formatted_sink_backend< char, concurrent_feeding >
{
    //! Base type
    typedef basic_formatted_sink_backend< char, concurrent_feeding > base_type;

public:
    //! Character type
    typedef base_type::char_type char_type;
    //! String type to be used as a message text holder
    typedef base_type::string_type string_type;
    //! Interprocess message queue type
    typedef QueueT queue_type;

private:
    //! Interprocess queue
    queue_type m_queue;

public:
    /*!
     * Default constructor. The method constructs the backend using default values
     * of all the parameters.
     */
    text_ipc_message_queue_backend() BOOST_NOEXCEPT
    {
    }

    /*!
     * Constructor. The method creates a backend that sends messages to an associated
     * message queue. After the call, the backend will be in running state if a message
     * queue is successfully associated.
     *
     * The following named parameters are supported:
     *
     * \li \c message_queue_name - Specifies the name of the queue. The name is given as a C-style string.
     *                             A valid name is one that can be used as a C++ identifier or is a keyword.
     *                             If an empty string is passed, the backend has no associated message queue,
     *                             and does not send any message. Default is an empty string. On Windows
     *                             platforms, the name is used to compose kernel object names, and you may
     *                             need to add the "Global\" prefix to the name in certain cases.
     * \li \c open_mode - Specifies the open mode which is given as a \c open_mode value. If a new message queue
     *                    is created, the specified message queue settings are applied. If an existing message queue
     *                    is opened, the specified settings are ignored. The default is \c open_only. For
     *                    \c open_or_create, users can query whether the message queue is actually opened or created
     *                    by checking the value of \c errno. It would be \c EEXIST when opened, and \c ENOENT when
     *                    created. The \c errno is set to \c 0 before doing any operation. For \c create_only, the
     *                    operation may fail with \c errno \c EEXIST. For \c open_only, the operation may fail with
     *                    \c errno \c ENOENT. For any other error, a <tt>boost::system::system_error</tt> exception
     *                    is thrown.
     * \li \c max_queue_size - Specifies the maximum number of messages the message queue can hold. The parameter
     *                          is given as an <tt>unsigned int</tt> value, with the default being \c 10.
     * \li \c max_message_size - Specifies the maximum size in bytes of each message allowed by the message
     *                           queue. The parameter is given as an <tt>unsigned int</tt> value, with the default
     *                           being \c 1000.
     * \li \c queue_policy - Specifies the policy to use when sending to a full message queue. The parameter is
     *                       given as a \c queue_policy_type value, with the default being \c drop_when_full.
     * \li \c message_policy - Specifies the policy to use when the message to send is too long for the associated
     *                         message queue. The parameter is given as a \c message_policy_type value, with the
     *                         default being \c throw_when_too_long.
     * \li \c permissions - Specifies access permissions for the associated message queue if it is created by this
     *                      object. The parameter is ignored otherwise. The parameter is of type \c permissions with
     *                      the default being a default-constructed \c permissions object.
     */
#ifndef BOOST_LOG_DOXYGEN_PASS
    BOOST_LOG_PARAMETRIZED_CONSTRUCTORS_CALL(text_ipc_message_queue_backend, construct)
#else
    template< typename... ArgsT >
    explicit text_ipc_message_queue_backend(ArgsT... const& args);
#endif

    /*!
     * The method returns a reference to the managed \c queue_type object.
     *
     * \return A reference to the managed \c queue_type object.
     */
    queue_type& message_queue() BOOST_NOEXCEPT { return m_queue; }

    /*!
     * The method returns a const reference to the managed \c queue_type object.
     *
     * \return A const reference to the managed \c queue_type object.
     */
    queue_type const& message_queue() const BOOST_NOEXCEPT { return m_queue; }

    /*!
     * The method sets the message queue to be associated with the object. If the object is
     * associated with a message queue, <tt>close()</tt> is first called and the precondition
     * to calling <tt>close()</tt> applies. After the call, the object will be in running state
     * if a message queue is successfully associated. If no message queue is associated after
     * the call, the backend does not send any message.
     *
     * \param name Name of the message queue to be associated with. A valid name is one
     *             that can be used as a C++ identifier or is a keyword. Passing an empty
     *             string is equivalent to calling <tt>close()</tt>. On Windows platforms,
     *             the name is used to compose kernel object names, and you may need to
     *             add the "Global\" prefix to the name in certain cases.
     * \param mode Open mode. If a new message queue is created, the specified settings, as indicated
     *             by the remaining parameters, are applied. If an existing message queue is opened,
     *             the specified settings are ignored. For \c open_or_create, users can query whether
     *             the message queue is actually opened or created by checking the value of \c errno.
     *             It would be \c EEXIST when opened, and \c ENOENT when created. The \c errno is set
     *             to \c 0 before doing any operation. For \c create_only, the operation may fail
     *             with \c errno \c EEXIST. For \c open_only, the operation may fail with \c errno
     *             \c ENOENT. For any other error, a <tt>boost::system::system_error</tt> exception
     *             is thrown.
     * \param max_queue_size Maximum number of messages the queue can hold.
     * \param max_message_size Maximum size in bytes of each message allowed by the queue.
     * \param perms Access permissions for the associated message queue if it is
     *              created by this object. The parameter is ignored otherwise.
     *
     * \return \c true if the operation is successful, and \c false otherwise.
     */
    void create(
        char const* name,
        uint32_t capacity,
        uint32_t max_message_size = 1000,
        permissions const& perms = permissions());

    /*!
     * Tests whether the object is associated with any message queue. Only when the backend has
     * an associated message queue, will any message be sent.
     *
     * \return \c true if the object is associated with a message queue, and \c false otherwise.
     */
    bool is_open() const BOOST_NOEXCEPT { return m_queue.is_open(); }

    /*!
     * The method wakes up all threads that are blocking on calls to <tt>consume()</tt>.
     * Those calls would then return, dropping the messages they are trying to send.
     * Note that, the method does not block until the woke-up threads have actually
     * returned from <tt>consume()</tt>. Other means is needed to ensure that calls to
     * <tt>consume()</tt> have returned. The method also puts the object in stopped
     * state. When in stopped state, calls to <tt>consume()</tt> will return immediately,
     * dropping the messages they are trying to send, when they would otherwise block in
     * running state. Concurrent calls to this method and <tt>consume()</tt> are OK.
     */
    void stop()
    {
        m_queue.stop();
    }

    /*!
     * The method puts the object in running state where calls to <tt>consume()</tt>
     * may block.
     */
    void reset()
    {
        m_queue.reset();
    }

    /*!
     * The method disassociates the associated message queue, if any. No other threads
     * should be using this object before calling this method. The <tt>stop()</tt> method
     * could be used to have any threads currently blocking on <tt>consume()</tt> return,
     * and prevent further calls to <tt>consume()</tt> from blocking. The associated message
     * queue is destroyed, if the object represents the last outstanding reference to it.
     */
    void close()
    {
        m_queue.close();
    }

    /*!
     * The method queries the current queue policy.
     *
     * \return Current queue policy.
     */
    ipc::queue_overflow_policy queue_overflow_policy() const BOOST_NOEXCEPT { return OverflowPolicyV; }

    /*!
     * The method writes the message to the backend. Concurrent calls to this method
     * are OK. Therefore, the backend may be used with unlocked frontend. <tt>stop()</tt>
     * can be used to have a blocked <tt>consume()</tt> call return and prevent future
     * calls to <tt>consume()</tt> from blocking.
     */
    void consume(record_view const&, string_type const& formatted_message)
    {
        if (m_queue.is_open())
        {
            if (OverflowPolicyV == ipc::block_when_full)
            {
                m_queue.send(formatted_message.data(), formatted_message.size());
            }
            else
            {
                if (!m_queue.try_send(formatted_message.data(), formatted_message.size()))
                {
                    if (OverflowPolicyV == ipc::throw_when_full)
                        BOOST_THROW_EXCEPTION(runtime_error("Interprocess message queue is full"));
                }
            }
        }
    }

private:
#ifndef BOOST_LOG_DOXYGEN_PASS
    //! Constructor implementation
    template< typename ArgsT >
    void construct(ArgsT const& args)
    {
        construct(
            args[keywords::message_queue_name | ""],
            args[keywords::open_mode          | open_only],
            args[keywords::max_queue_size     | 10],
            args[keywords::max_message_size   | 1000],
            args[keywords::queue_policy       | drop_when_full],
            args[keywords::message_policy     | throw_when_too_long],
            args[keywords::permissions        | permissions()]);
    }
    //! Constructor implementation
    BOOST_LOG_API void construct(
        char const* message_queue_name,
        open_mode mode,
        unsigned int max_queue_size,
        unsigned int max_message_size,
        queue_policy_type queue_policy_val,
        message_policy_type message_policy_val,
        permissions const& perms);
#endif // BOOST_LOG_DOXYGEN_PASS
};

} // namespace sinks

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_
