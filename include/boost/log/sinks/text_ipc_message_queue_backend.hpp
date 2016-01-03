/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   text_ipc_message_queue_backend.hpp
 * \author Lingxi Li
 * \date   14.10.2015
 *
 * The header contains implementation of a text interprocess message queue sink
 * backend along with implementation of a supporting interprocess message queue.
 */

#ifndef BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_
#define BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_

#include <string>
#include <boost/move/move.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
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

/*!
 * \brief An implementation of a text interprocess message queue sink backend and
 *        a supporting interprocess message queue.
 *
 * The sink backend sends formatted log messages to an interprocess message queue
 * which can be extracted by a viewer process. Methods of this class are not
 * thread-safe, unless otherwise specified.
 */
template < typename CharT >
class basic_text_ipc_message_queue_backend :
    public basic_formatted_sink_backend< CharT, concurrent_feeding >
{
    //! Base type
    typedef basic_formatted_sink_backend< CharT > base_type;

public:
    //! Character type
    typedef typename base_type::char_type char_type;
    //! String type to be used as a message text holder
    typedef typename base_type::string_type string_type;


    //! Convenient typedef for <tt>message_queue_type::open_mode</tt>.
    typedef typename message_queue_type::open_mode open_mode;
    //! Convenient open mode value imported from \c message_queue_type.
    BOOST_LOG_API static open_mode const create_only = message_queue_type::create_only;
    //! Convenient open mode value imported from \c message_queue_type.
    BOOST_LOG_API static open_mode const open_only = message_queue_type::open_only;
    //! Convenient open mode value imported from \c message_queue_type.
    BOOST_LOG_API static open_mode const open_or_create = message_queue_type::open_or_create;

    //! Queue policy type
    enum queue_policy_type
    {
        //! Drop the message when the queue is full (default)
        drop_when_full,
        //! Throw an exception when the queue is full
        throw_when_full,
        //! Block the send operation when the queue is full
        block_when_full
    };
    //! Message policy type
    enum message_policy_type
    {
        //! Throw an exception when the message is too long for the queue (default)
        throw_when_too_long,
        //! Drop the the message when it is too long for the queue
        drop_when_too_long,
        //! Truncate the message when it is too long for the queue
        truncate_when_too_long
    };

private:
    //! \cond

    struct implementation;
    implementation* m_pImpl;

    //! \endcond

public:
    /*!
     * Default constructor. The method constructs the backend using default values
     * of all the parameters.
     */
    BOOST_LOG_API basic_text_ipc_message_queue_backend();

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
    BOOST_LOG_PARAMETRIZED_CONSTRUCTORS_CALL(basic_text_ipc_message_queue_backend, construct)
#else
    template< typename... ArgsT >
    explicit basic_text_ipc_message_queue_backend(ArgsT... const& args);
#endif

    /*!
     * Destructor. Calls <tt>close()</tt> and the precondition to calling
     * <tt>close()</tt> applies.
     */
    BOOST_LOG_API ~basic_text_ipc_message_queue_backend();

    /*!
     * The method returns a reference to the managed \c message_queue_type object.
     *
     * \return A reference to the managed \c message_queue_type object.
     */
    BOOST_LOG_API message_queue_type& message_queue();

    /*!
     * The method returns a const reference to the managed \c message_queue_type object.
     *
     * \return A const reference to the managed \c message_queue_type object.
     */
    BOOST_LOG_API message_queue_type const& message_queue() const;

    /*!
     * The method returns the name of the associated message queue.
     *
     * \return Name of the associated message queue, or an empty string if there
     *         is no associated message queue.
     */
    BOOST_LOG_API std::string name() const;

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
    BOOST_LOG_API bool open(
        char const* name, open_mode mode = open_only,
        unsigned int max_queue_size = 10, unsigned int max_message_size = 1000,
        permissions const& perms = permissions());

    /*!
     * Tests whether the object is associated with any message queue. Only when the backend has
     * an associated message queue, will any message be sent.
     *
     * \return \c true if the object is associated with a message queue, and \c false otherwise.
     */
    BOOST_LOG_API bool is_open() const;

    /*!
     * The method returns the maximum number of messages the associated message queue
     * can hold. Note that the returned value may be different from the corresponding
     * value passed to the constructor or <tt>open()</tt>, for the message queue may
     * not be created by this object. Throws <tt>std::logic_error</tt> if the object
     * is not associated with any message queue.
     *
     * \return Maximum number of messages the associated message queue can hold.
     */
    BOOST_LOG_API unsigned int max_queue_size() const;

    /*!
     * The method returns the maximum size in bytes of each message allowed by the
     * associated message queue. Note that the returned value may be different from the
     * corresponding value passed to the constructor or <tt>open()</tt>, for the
     * message queue may not be created by this object. Throws <tt>std::logic_error</tt>
     * if the object is not associated with any message queue.
     *
     * \return Maximum size in bytes of each message allowed by the associated message
     *         queue.
     */
    BOOST_LOG_API unsigned int max_message_size() const;

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
    BOOST_LOG_API void stop();

    /*!
     * The method puts the object in running state where calls to <tt>consume()</tt>
     * may block. This method is thread-safe.
     */
    BOOST_LOG_API void reset();

    /*!
     * The method disassociates the associated message queue, if any. No other threads
     * should be using this object before calling this method. The <tt>stop()</tt> method
     * could be used to have any threads currently blocking on <tt>consume()</tt> return,
     * and prevent further calls to <tt>consume()</tt> from blocking. The associated message
     * queue is destroyed, if the object represents the last outstanding reference to it.
     */
    BOOST_LOG_API void close();

    /*!
     * The method sets the policy to use when sending to a full message queue.
     *
     * \param policy The queue policy to set.
     */
    BOOST_LOG_API void set_queue_policy(queue_policy_type policy);

    /*!
     * The method sets the policy to use when the message to send is too long for
     * the associated message queue.
     *
     * \param policy The message policy to set.
     */
    BOOST_LOG_API void set_message_policy(message_policy_type policy);

    /*!
     * The method queries the current queue policy.
     *
     * \return Current queue policy.
     */
    BOOST_LOG_API queue_policy_type queue_policy() const;

    /*!
     * The method queries the current message policy.
     *
     * \return Current message policy.
     */
    BOOST_LOG_API message_policy_type message_policy() const;

    /*!
     * The method writes the message to the backend. Concurrent calls to this method
     * are OK. Therefore, the backend may be used with unlocked frontend. <tt>stop()</tt>
     * can be used to have a blocked <tt>consume()</tt> call return and prevent future
     * calls to <tt>consume()</tt> from blocking.
     */
    BOOST_LOG_API void consume(record_view const& rec, string_type const& formatted_message);

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

#ifdef BOOST_LOG_USE_CHAR
typedef basic_text_ipc_message_queue_backend< char > text_ipc_message_queue_backend;      //!< Convenience typedef for narrow-character logging
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
typedef basic_text_ipc_message_queue_backend< wchar_t > wtext_ipc_message_queue_backend;  //!< Convenience typedef for wide-character logging
#endif

} // namespace sinks

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_SINKS_TEXT_IPC_MESSAGE_QUEUE_BACKEND_HPP_INCLUDED_
