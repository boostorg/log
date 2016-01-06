/*
 *                 Copyright Lingxi Li 2015.
 *              Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   utility/ipc/reliable_message_queue.hpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   01.01.2016
 *
 * The header contains declaration of a reliable interprocess message queue.
 */

#ifndef BOOST_LOG_UTILITY_IPC_RELIABLE_MESSAGE_QUEUE_HPP_INCLUDED_
#define BOOST_LOG_UTILITY_IPC_RELIABLE_MESSAGE_QUEUE_HPP_INCLUDED_

#include <boost/log/detail/config.hpp>
#include <cstddef>
#include <boost/cstdint.hpp>
#include <boost/move/core.hpp>
#include <boost/log/utility/open_mode.hpp>
#include <boost/log/utility/permissions.hpp>
#include <boost/log/detail/header.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace ipc {

/*!
 * \brief An implementation of a supporting interprocess message queue used
 *        by \c basic_text_ipc_message_queue_backend. Methods of this class
 *        are not thread-safe, unless otherwise specified.
 */
class reliable_message_queue
{
#if !defined(BOOST_LOG_DOXYGEN_PASS)

    BOOST_MOVABLE_BUT_NOT_COPYABLE(reliable_message_queue)

private:
    struct implementation;
    implementation* m_impl;

#endif // !defined(BOOST_LOG_DOXYGEN_PASS)

public:
    /*!
     * Default constructor. The method constructs an object that is not associated with any
     * message queue.
     *
     * \post <tt>is_open() == false</tt>
     */
    reliable_message_queue() BOOST_NOEXCEPT : m_impl(NULL)
    {
    }

    /*!
     * Constructor. The method is used to construct an object and create the associated
     * message queue. The constructed object will be in running state if the message queue is
     * successfully created.
     *
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with. A valid name is one that
     *             can be used as a C++ identifier or is a keyword.
     *             On Windows platforms, the name is used to compose kernel object names, and
     *             you may need to add the "Global\" prefix to the name in certain cases.
     * \param capacity Maximum number of allocation blocks the queue can hold.
     * \param block_size Size in bytes of allocation block. Must be a power of 2.
     * \param perms Access permissions for the associated message queue.
     */
    reliable_message_queue
    (
        open_mode::create_only_tag,
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms = permissions()
    ) :
        m_impl(NULL)
    {
        this->create(name, capacity, block_size, perms);
    }

    /*!
     * Constructor. The method is used to construct an object and create or open the associated
     * message queue. The constructed object will be in running state if the message queue is
     * successfully created or opened. If the message queue that is identified by the name already
     * exists then the other queue parameters are ignored. The actual queue parameters can be obtained
     * with accessors from the constructed object.
     *
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with. A valid name is one that
     *             can be used as a C++ identifier or is a keyword.
     *             On Windows platforms, the name is used to compose kernel object names, and
     *             you may need to add the "Global\" prefix to the name in certain cases.
     * \param capacity Maximum number of allocation blocks the queue can hold.
     * \param block_size Size in bytes of allocation block. Must be a power of 2.
     * \param perms Access permissions for the associated message queue.
     */
    reliable_message_queue
    (
        open_mode::open_or_create_tag,
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms = permissions()
    ) :
        m_impl(NULL)
    {
        this->open_or_create(name, capacity, block_size, perms);
    }

    /*!
     * Constructor. The method is used to construct an object and open the existing
     * message queue. The constructed object will be in running state if the message queue is
     * successfully opened.
     *
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with.
     */
    reliable_message_queue(open_mode::open_only_tag, char const* name) :
        m_impl(NULL)
    {
        this->open(name);
    }

    /*!
     * Destructor. Calls <tt>close()</tt>.
     */
    ~reliable_message_queue() BOOST_NOEXCEPT
    {
        this->close();
    }

    /*!
     * Move constructor. The method move-constructs an object from \c other. After
     * the call, the constructed object becomes \c other, while \c other is left in
     * default constructed state.
     *
     * \param that The object to be moved.
     */
    reliable_message_queue(BOOST_RV_REF(reliable_message_queue) that) BOOST_NOEXCEPT :
        m_impl(that.m_impl)
    {
        that.m_impl = NULL;
    }

    /*!
     * Move assignment operator. If the object is associated with a message queue,
     * <tt>close()</tt> is first called and the precondition to calling <tt>close()</tt>
     * applies. After the call, the object becomes \a that while \a that is left
     * in default constructed state.
     *
     * \param that The object to be moved.
     *
     * \return A reference to the assigned object.
     */
    reliable_message_queue& operator= (BOOST_RV_REF(reliable_message_queue) that) BOOST_NOEXCEPT
    {
        reliable_message_queue other(static_cast< BOOST_RV_REF(reliable_message_queue) >(that));
        this->swap(other);
        return *this;
    }

    /*!
     * The method swaps the object with \c other.
     *
     * \param that The other object to swap with.
     */
    void swap(reliable_message_queue& that) BOOST_NOEXCEPT
    {
        implementation* p = m_impl;
        m_impl = that.m_impl;
        that.m_impl = p;
    }

    //! Swaps the two \c reliable_message_queue objects.
    friend void swap(reliable_message_queue& a, reliable_message_queue& b) BOOST_NOEXCEPT
    {
        a.swap(b);
    }

    /*!
     * The method creates the message queue to be associated with the object. After the call,
     * the object will be in running state if a message queue is successfully created.
     *
     * \pre <tt>is_open() == false</tt>
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with. A valid name is one
     *             that can be used as a C++ identifier or is a keyword.
     *             On Windows platforms, the name is used to compose kernel object names,
     *             and you may need to add the "Global\" prefix to the name in certain cases.
     * \param capacity Maximum number of allocation blocks the queue can hold.
     * \param block_size Size in bytes of allocation block. Must be a power of 2.
     * \param perms Access permissions for the associated message queue.
     */
    BOOST_LOG_API void create
    (
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms = permissions()
    );

    /*!
     * The method creates or opens the message queue to be associated with the object.
     * After the call, the object will be in running state if a message queue is successfully
     * created or opened. If the message queue that is identified by the name already exists then
     * the other queue parameters are ignored. The actual queue parameters can be obtained
     * with accessors from this object after this method returns.
     *
     * \pre <tt>is_open() == false</tt>
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with. A valid name is one
     *             that can be used as a C++ identifier or is a keyword.
     *             On Windows platforms, the name is used to compose kernel object names,
     *             and you may need to add the "Global\" prefix to the name in certain cases.
     * \param capacity Maximum number of allocation blocks the queue can hold.
     * \param block_size Size in bytes of allocation block. Must be a power of 2.
     * \param perms Access permissions for the associated message queue.
     */
    BOOST_LOG_API void open_or_create
    (
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms = permissions()
    );

    /*!
     * The method opens the existing message queue to be associated with the object.
     * After the call, the object will be in running state if a message queue is successfully
     * opened.
     *
     * \pre <tt>is_open() == false</tt>
     * \post <tt>is_open() == true</tt>
     *
     * \param name Name of the message queue to be associated with.
     */
    BOOST_LOG_API void open(char const* name);

    /*!
     * Tests whether the object is associated with any message queue.
     *
     * \return \c true if the object is associated with a message queue, and \c false otherwise.
     */
    bool is_open() const BOOST_NOEXCEPT
    {
        return m_impl != NULL;
    }

    /*!
     * This method empties the associated message queue. Throws <tt>std::logic_error</tt> if there
     * is no associated message queue. Concurrent calls to this method, <tt>send()</tt>,
     * <tt>try_send()</tt>, <tt>receive()</tt>, <tt>try_receive()</tt>, and <tt>stop()</tt> are OK.
     *
     * \pre <tt>is_open() == true</tt>
     */
    BOOST_LOG_API void clear();

    /*!
     * The method returns the name of the associated message queue.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \return Name of the associated message queue
     */
    BOOST_LOG_API const char* name() const;

    /*!
     * The method returns the maximum number of allocation blocks the associated message queue
     * can hold. Note that the returned value may be different from the corresponding
     * value passed to the constructor or <tt>open_or_create()</tt>, for the message queue may
     * not have been created by this object.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \return Maximum number of allocation blocks the associated message queue can hold.
     */
    BOOST_LOG_API uint32_t capacity() const;

    /*!
     * The method returns the allocation block size, in bytes. Each message in the
     * associated message queue consumes an integer number of allocation blocks.
     * Note that the returned value may be different from the corresponding value passed
     * to the constructor or <tt>open_or_create()</tt>, for the message queue may not
     * have been created by this object.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \return Allocation block size, in bytes.
     */
    BOOST_LOG_API uint32_t block_size() const;

    /*!
     * The method wakes up all threads that are blocked in calls to <tt>send()</tt> or
     * <tt>receive()</tt>. Those calls would then return \c false with \c errno \c EINTR.
     * Note that, the method does not block until the woken-up threads have actually
     * returned from <tt>send()</tt> or <tt>receive()</tt>. Other means is needed to ensure
     * that calls to <tt>send()</tt> or <tt>receive()</tt> have returned, e.g., joining the
     * threads that might be blocking on the calls.
     *
     * The method also puts the object in stopped state. When in stopped state, calls to
     * <tt>send()</tt> or <tt>receive()</tt> will return immediately with return value
     * \c false and \c errno \c EINTR when they would otherwise block in running state.
     *
     * Concurrent calls to this method, <tt>send()</tt>, <tt>try_send()</tt>, <tt>receive()</tt>,
     * <tt>try_receive()</tt>, and <tt>clear()</tt> are allowed.
     *
     * \pre <tt>is_open() == true</tt>
     */
    BOOST_LOG_API void stop();

    /*!
     * The method puts the object in running state where calls to <tt>send()</tt> or
     * <tt>receive()</tt> may block. This method is thread-safe.
     *
     * \pre <tt>is_open() == true</tt>
     */
    BOOST_LOG_API void reset();

    /*!
     * The method disassociates the associated message queue, if any. No other threads
     * should be using this object before calling this method. The <tt>stop()</tt> method
     * can be used to have any threads currently blocked in <tt>send()</tt> or
     * <tt>receive()</tt> return, and prevent further calls to them from blocking. Typically,
     * before calling this method, one would first call <tt>stop()</tt> and then join all
     * threads that might be blocking on <tt>send()</tt> or <tt>receive()</tt> to ensure that
     * they have returned from the calls. The associated message queue is destroyed if the
     * object represents the last outstanding reference to it.
     *
     * \post <tt>is_open() == false</tt>
     */
    void close() BOOST_NOEXCEPT
    {
        if (is_open())
            do_close();
    }

    /*!
     * The method sends a message to the associated message queue. When the object is in
     * running state and the queue is full, the method blocks. The blocking is interrupted
     * when <tt>stop()</tt> is called, in which case the method returns \c false with
     * \c errno \c EINTR. When the object is in stopped state and the queue is full, the
     * method does not block but returns immediately with return value \c false and \c errno
     * \c EINTR. If the object is not associated with any message queue, an <tt>std::logic_error</tt>
     * exception is thrown. <tt>boost::system::system_error</tt> is thrown for errors resulting
     * from native operating system calls. It is possible to send an empty message by passing
     * \c 0 to the parameter \c message_size. Concurrent calls to <tt>send()</tt>, <tt>try_send()</tt>,
     * <tt>receive()</tt>, <tt>try_receive()</tt>, <tt>stop()</tt>, and <tt>clear()</tt> are OK.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \param message_data The message data to send. Ignored when \c message_size is \c 0.
     * \param message_size Size of the message data in bytes. If the size is larger than the
     *                     maximum size allowed by the associated message queue, an
     *                     <tt>std::logic_error</tt> exception is thrown.
     *
     * \return \c true if the operation is successful, and \c false otherwise.
     */
    BOOST_LOG_API bool send(void const* message_data, uint32_t message_size);

    /*!
     * The method performs an attempt to send a message to the associated message queue.
     * The method is non-blocking, and always returns immediately. If the object is not
     * associated with any message queue, an <tt>std::logic_error</tt> exception is thrown.
     * <tt>boost::system::system_error</tt> is thrown for errors resulting from native
     * operating system calls. Note that it is possible to send an empty message by passing
     * \c 0 to the parameter \c message_size. Concurrent calls to <tt>send()</tt>,
     * <tt>try_send()</tt>, <tt>receive()</tt>, <tt>try_receive()</tt>, <tt>stop()</tt>,
     * and <tt>clear()</tt> are OK.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \param message_data The message data to send. Ignored when \c message_size is \c 0.
     * \param message_size Size of the message data in bytes. If the size is larger than the
     *                     maximum size allowed by the associated message queue, an
     *                     <tt>std::logic_error</tt> exception is thrown.
     *
     * \return \c true if the message is successfully sent, and \c false otherwise (e.g.,
     *         when the queue is full).
     */
    BOOST_LOG_API bool try_send(void const* message_data, uint32_t message_size);

    /*!
     * The method takes a message from the associated message queue. When the object is in
     * running state and the queue is empty, the method blocks. The blocking is interrupted
     * when <tt>stop()</tt> is called, in which case the method returns \c false with
     * \c errno \c EINTR. When the object is in stopped state and the queue is empty, the
     * method does not block but returns immediately with return value \c false and \c errno
     * \c EINTR. If the object is not associated with any message queue, an <tt>std::logic_error</tt>
     * exception is thrown. <tt>boost::system::system_error</tt> is thrown for errors resulting
     * from native operating system calls. Concurrent calls to <tt>send()</tt>, <tt>try_send()</tt>,
     * <tt>receive()</tt>, <tt>try_receive()</tt>, <tt>stop()</tt>, and <tt>clear()</tt> are OK.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \param buffer The memory buffer to store the received message.
     * \param buffer_size The size of the buffer in bytes. This parameter should be no smaller
     *                    than the maximum message size allowed by the associated message queue.
     *                    Otherwise, an <tt>std::logic_error</tt> exception is thrown.
     * \param message_size Receives the size of the received message, in bytes.
     *
     * \return \c true if the operation is successful, and \c false otherwise.
     */
    BOOST_LOG_API bool receive(void* buffer, uint32_t buffer_size, uint32_t& message_size);

    /*!
     * The method performs an attempt to take a message from the associated message queue. The
     * method is non-blocking, and always returns immediately. If the object is not associated
     * with any message queue, an <tt>std::logic_error</tt> exception is thrown.
     * <tt>boost::system::system_error</tt> is thrown for errors resulting from native operating
     * system calls. Concurrent calls to <tt>send()</tt>, <tt>try_send()</tt>, <tt>receive()</tt>,
     * <tt>try_receive()</tt>, <tt>stop()</tt>, and <tt>clear()</tt> are OK.
     *
     * \pre <tt>is_open() == true</tt>
     *
     * \param buffer The memory buffer to store the received message.
     * \param buffer_size The size of the buffer in bytes. This parameter should be no smaller
     *                    than the maximum message size allowed by the associated message queue.
     *                    Otherwise, an <tt>std::logic_error</tt> exception is thrown.
     * \param message_size Receives the size of the received message, in bytes.
     *
     * \return \c true if a message is successfully received, and \c false otherwise (e.g.,
     *         when the queue is empty).
     */
    BOOST_LOG_API bool try_receive(void* buffer, uint32_t buffer_size, uint32_t& message_size);

#if !defined(BOOST_LOG_DOXYGEN_PASS)
private:
    //! Closes the message queue, if it's open
    BOOST_LOG_API void do_close() BOOST_NOEXCEPT;
#endif
};

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_UTILITY_IPC_RELIABLE_MESSAGE_QUEUE_HPP_INCLUDED_
