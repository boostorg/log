/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   ipc_message_queue_posix.hpp
 * \author Lingxi Li
 * \date   17.11.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * This file provides an IPC message queue implementation on POSIX platforms,
 * and is for inclusion into text_ipc_message_queue_backend.cpp.
 */

#ifndef BOOST_LOG_IPC_MESSAGE_QUEUE_POSIX_HPP_INCLUDED_
#define BOOST_LOG_IPC_MESSAGE_QUEUE_POSIX_HPP_INCLUDED_

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <string>
#include <boost/atomic.hpp>
#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>
#include "posix_wrapper.hpp"
#include <boost/log/detail/header.hpp>

#ifndef BOOST_WINDOWS

#include <sched.h>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace sinks {

namespace {

typedef unsigned char byte;

class mutex_attr_type
{
public:
    mutex_attr_type()
    {
        aux::pthread_mutexattr_init(ptr());
    }
    ~mutex_attr_type()
    {
        if (pthread_mutexattr_destroy(ptr()) != 0)
        {
            std::terminate();
        }
    }
    pthread_mutexattr_t* ptr()
    {
        return &m_Value;
    }
    pthread_mutexattr_t const* ptr() const
    {
        return &m_Value;
    }

private:
    pthread_mutexattr_t m_Value;
};

class cond_attr_type
{
public:
    cond_attr_type()
    {
        aux::pthread_condattr_init(ptr());
    }
    ~cond_attr_type()
    {
        if (pthread_condattr_destroy(ptr()) != 0)
        {
            std::terminate();
        }
    }
    pthread_condattr_t* ptr()
    {
        return &m_Value;
    }
    pthread_condattr_t const* ptr() const
    {
        return &m_Value;
    }

private:
    pthread_condattr_t m_Value;
};

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////
//  Permission implementation
////////////////////////////////////////////////////////////////////////////////
//! Permission implementation data
template < typename CharT >
struct basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::implementation
{
    mode_t m_Mode;
};

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::permission()
  : m_pImpl(new implementation())
{
    m_pImpl->m_Mode = 0644;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::permission(
  mode_t mode)
  : m_pImpl(new implementation())
{
    m_pImpl->m_Mode = mode;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::~permission()
{
    delete m_pImpl;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::permission(
  permission const& other)
  : m_pImpl(new implementation())
{
    m_pImpl->m_Mode = other.m_pImpl->m_Mode;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::permission(
  BOOST_RV_REF(permission) other)
  : m_pImpl(new implementation())
{
    m_pImpl->m_Mode = 0644;
    swap(other);
}

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission&
basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::operator =(permission const& other)
{
    m_pImpl->m_Mode = other.m_pImpl->m_Mode;
    return *this;
}

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission&
basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::operator =(BOOST_RV_REF(permission) other)
{
    if (this != &other)
    {
        m_pImpl->m_Mode = 0644;
        swap(other);
    }
    return *this;
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::permission::swap(permission& other)
{
    std::swap(m_pImpl, other.m_pImpl);
}

////////////////////////////////////////////////////////////////////////////////
//  Interprocess message queue implementation
////////////////////////////////////////////////////////////////////////////////
//! Message queue implementation data
template < typename CharT >
struct basic_text_ipc_message_queue_backend< CharT >::message_queue_type::implementation
{
    struct header
    {
        atomic_bool     m_Created;
        unsigned int    m_MaxQueueSize;
        unsigned int    m_MaxMessageSize;
        pthread_mutex_t m_Mutex;
        unsigned int    m_RefCount;
        pthread_cond_t  m_NonEmptyQueue;
        pthread_cond_t  m_NonFullQueue;
        unsigned int    m_QueueSize;
        unsigned int    m_PutPos;
        unsigned int    m_GetPos;
    };

    atomic_bool m_fStop;
    std::string m_Name;
    int m_fdSharedMemory;
    header* m_pHeader;

    static mode_t get_mode(permission const& perm)
    {
        return perm.m_pImpl->m_Mode;
    }

    implementation()
      : m_fStop(true)
      , m_fdSharedMemory(-1)
      , m_pHeader(NULL)
    {
    }

    ~implementation()
    {
        try
        {
            close();
        }
        catch (...)
        {
            std::terminate();
        }
    }

    void clear_queue()
    {
        m_pHeader->m_QueueSize = 0;
        m_pHeader->m_PutPos = 0;
        m_pHeader->m_GetPos = 0;
        aux::pthread_cond_broadcast(&m_pHeader->m_NonFullQueue);
    }

    void stop()
    {
        bool locked = false;
        try
        {
            if (!is_open()) throw std::logic_error("IPC message queue not opened");
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            m_fStop = true;
            aux::pthread_cond_broadcast(&m_pHeader->m_NonEmptyQueue);
            aux::pthread_cond_broadcast(&m_pHeader->m_NonFullQueue);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
        }
        catch (...)
        {
            if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }

    void reset()
    {
        m_fStop = false;
    }

    void close()
    {
        if (is_open())
        {
            unsigned int memory_size = sizeof(header) +
                (sizeof(unsigned int) + m_pHeader->m_MaxMessageSize) *
                m_pHeader->m_MaxQueueSize;
            bool locked = false;
            try
            {
                int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
                locked = true;
                if (err == EOWNERDEAD)
                {
                    clear_queue();
                    aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
                }
                if (--m_pHeader->m_RefCount == 0) aux::shm_unlink(m_Name.c_str());
                aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                locked = false;
                aux::munmap(m_pHeader, memory_size);
                m_pHeader = NULL;
                aux::close(m_fdSharedMemory);
                m_fdSharedMemory = -1;
                m_Name.clear();
            }
            catch (...)
            {
                if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                throw;
            }
        }
    }

    void create_message_queue(
      unsigned int max_queue_size, unsigned int max_message_size,
      permission const& permission_value)
    {
        unsigned int memory_size = sizeof(header) +
            (sizeof(unsigned int) + max_message_size) * max_queue_size;
        bool mutex_inited = false;
        bool non_empty_queue_inited = false;
        bool non_full_queue_inited = false;
        try
        {
            m_fdSharedMemory = aux::shm_open(m_Name.c_str(), O_RDWR | O_CREAT | O_EXCL,
                get_mode(permission_value));
            // Enter critical section
            aux::ftruncate(m_fdSharedMemory, memory_size);
            m_pHeader = aux::typed_mmap< header* >(
                NULL, memory_size, PROT_READ | PROT_WRITE,
                MAP_SHARED, m_fdSharedMemory, 0);

            mutex_attr_type mutex_attr;
            aux::pthread_mutexattr_settype(mutex_attr.ptr(), PTHREAD_MUTEX_NORMAL);
            aux::pthread_mutexattr_setpshared(mutex_attr.ptr(), PTHREAD_PROCESS_SHARED);
            aux::pthread_mutexattr_setrobust(mutex_attr.ptr(), PTHREAD_MUTEX_ROBUST);
            aux::pthread_mutex_init(&m_pHeader->m_Mutex, mutex_attr.ptr());
            mutex_inited = true;

            cond_attr_type cond_attr;
            aux::pthread_condattr_setpshared(cond_attr.ptr(), PTHREAD_PROCESS_SHARED);
            aux::pthread_cond_init(&m_pHeader->m_NonEmptyQueue, cond_attr.ptr());
            non_empty_queue_inited = true;
            aux::pthread_cond_init(&m_pHeader->m_NonFullQueue, cond_attr.ptr());
            non_full_queue_inited = true;

            m_pHeader->m_MaxQueueSize = max_queue_size;
            m_pHeader->m_MaxMessageSize = max_message_size;
            m_pHeader->m_RefCount = 1;
            m_pHeader->m_QueueSize = 0;
            m_pHeader->m_PutPos = 0;
            m_pHeader->m_GetPos = 0;
            m_pHeader->m_Created = true;
            // Leave section ends
        }
        catch (...)
        {
            if (mutex_inited) aux::pthread_mutex_destroy(&m_pHeader->m_Mutex);
            if (non_empty_queue_inited) aux::pthread_cond_destroy(&m_pHeader->m_NonEmptyQueue);
            if (non_full_queue_inited) aux::pthread_cond_destroy(&m_pHeader->m_NonFullQueue);
            aux::safe_munmap(m_pHeader, memory_size);
            if (m_fdSharedMemory >= 0)
            {
                aux::shm_unlink(m_Name.c_str());
                aux::close(m_fdSharedMemory);
                m_fdSharedMemory = -1;
            }
            m_Name.clear();
            throw;
        }
    }

    void open_message_queue(unsigned int max_queue_size, unsigned int max_message_size,
      permission const& permission_value)
    {
        bool locked = false;
        struct stat status = {};
        try
        {
            m_fdSharedMemory = aux::shm_open(m_Name.c_str(), O_RDWR, get_mode(permission_value));
            aux::fstat(m_fdSharedMemory, &status);
            if (!status.st_size) throw aux::make_system_error("shm_open", ENOENT);
            m_pHeader = aux::typed_mmap< header* >(
                NULL, status.st_size, PROT_READ | PROT_WRITE,
                MAP_SHARED, m_fdSharedMemory, 0);

            if (!m_pHeader->m_Created) throw aux::make_system_error("shm_open", ENOENT);
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            if (!m_pHeader->m_RefCount) throw aux::make_system_error("shm_open", ENOENT);
            ++m_pHeader->m_RefCount;
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
        }
        catch (...)
        {
            if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            aux::safe_munmap(m_pHeader, status.st_size);
            aux::safe_close(m_fdSharedMemory);
            m_Name.clear();
            throw;
        }
    }

    bool open(char const* name, open_mode mode, unsigned int max_queue_size,
      unsigned int max_message_size, permission const& permission_value)
    {
        close();
        errno = 0;

        if (*name)
        {
            m_Name = m_Name + "/" + name;
            try
            {
                if (mode == create_only)
                {
                    create_message_queue(
                        max_queue_size, max_message_size, permission_value);
                    errno = ENOENT;
                }
                else // open_only
                {
                    open_message_queue(
                        max_queue_size, max_message_size, permission_value);
                    errno = EEXIST;
                }
            }
            catch (system::system_error const& sys_err)
            {
                switch (sys_err.code().value())
                {
                case EEXIST:
                    errno = EEXIST;
                    break;
                case ENOENT:
                    errno = ENOENT;
                    break;
                default:
                    throw;
                }
                return false;
            }
            m_fStop = false;
            return true;
        }
        m_fStop = false;
        return true;
    }

    bool is_open() const
    {
        return m_pHeader;
    }

    void clear()
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        bool locked = false;
        try
        {
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            clear_queue();
            if (err == EOWNERDEAD) aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
        }
        catch (...)
        {
            if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }

    std::string name() const
    {
        return m_Name.size() ? m_Name.substr(1) : "";
    }

    unsigned int max_queue_size() const
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        return m_pHeader->m_MaxQueueSize;
    }

    unsigned int max_message_size() const
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        return m_pHeader->m_MaxMessageSize;
    }

    // Mutual exclusion should be guaranteed upon calling this function.
    void put_message(void const* message_data, unsigned int message_size)
    {
        byte* p = reinterpret_cast< byte* >(m_pHeader);
        p += sizeof(header) + (sizeof(unsigned int) + m_pHeader->m_MaxMessageSize) * m_pHeader->m_PutPos;
        std::memcpy(p, &message_size, sizeof(unsigned int));
        p += sizeof(unsigned int);
        std::memcpy(p, message_data, message_size);
        m_pHeader->m_PutPos = (m_pHeader->m_PutPos + 1) % m_pHeader->m_MaxQueueSize;
        ++m_pHeader->m_QueueSize;
        aux::pthread_cond_signal(&m_pHeader->m_NonEmptyQueue);
    }

    // Mutual exclusion should be guaranteed upon calling this function.
    void get_message(void* buffer, unsigned int, unsigned int& message_size)
    {
        byte* p = reinterpret_cast< byte* >(m_pHeader);
        p += sizeof(header) + (sizeof(unsigned int) + m_pHeader->m_MaxMessageSize) * m_pHeader->m_GetPos;
        std::memcpy(&message_size, p, sizeof(unsigned int));
        p += sizeof(unsigned int);
        std::memcpy(buffer, p, message_size);
        m_pHeader->m_GetPos = (m_pHeader->m_GetPos + 1) % m_pHeader->m_MaxQueueSize;
        --m_pHeader->m_QueueSize;
        aux::pthread_cond_signal(&m_pHeader->m_NonFullQueue);
    }

    bool send(void const* message_data, unsigned int message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (message_size > m_pHeader->m_MaxMessageSize) throw std::logic_error("Message is too long");
        errno = 0;
        bool locked = false;

        try
        {
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            while (m_pHeader->m_QueueSize >= m_pHeader->m_MaxQueueSize && !m_fStop) // full
            {
                aux::pthread_cond_wait(&m_pHeader->m_NonFullQueue, &m_pHeader->m_Mutex);
            }
            if (m_pHeader->m_QueueSize >= m_pHeader->m_MaxQueueSize)
            {
                aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                errno = EINTR;
                return false;
            }
            put_message(message_data, message_size);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            return true;
        }
        catch (...)
        {
            // The non-`aux::` version is used here for the suspicion that `aux::pthread_cond_wait()`
            // may throw with `m_Mutex` unlocked. `locked` is still set in this case, and we
            // end up with unlocking a mutex that is not locked at all. With the non-`aux::`
            // version, the unlock operation simply fails without throwing any exception, and
            // no harmful effects will ever happen.
            if (locked) pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }

    bool try_send(void const* message_data, unsigned int message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (message_size > m_pHeader->m_MaxMessageSize) throw std::logic_error("Message is too long");
        bool locked = false;
        try
        {
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            if (m_pHeader->m_QueueSize >= m_pHeader->m_MaxQueueSize)
            {
                aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                return false;
            }
            put_message(message_data, message_size);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            return true;
        }
        catch (...)
        {
            if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }

    bool receive(void* buffer, unsigned int buffer_size, unsigned int& message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (buffer_size < m_pHeader->m_MaxMessageSize) throw std::logic_error("Insufficient buffer");
        errno = 0;
        bool locked = false;

        try
        {
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            while (!m_pHeader->m_QueueSize && !m_fStop)
            {
                aux::pthread_cond_wait(&m_pHeader->m_NonEmptyQueue, &m_pHeader->m_Mutex);
            }
            if (!m_pHeader->m_QueueSize)
            {
                aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                errno = EINTR;
                return false;
            }
            get_message(buffer, buffer_size, message_size);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            return true;
        }
        catch (...)
        {
            // The non-`aux::` version is used here for the suspicion that `aux::pthread_cond_wait()`
            // may throw with `m_Mutex` unlocked. `locked` is still set in this case, and we
            // end up with unlocking a mutex that is not locked at all. With the non-`aux::`
            // version, the unlock operation simply fails without throwing any exception, and
            // no harmful effects will ever happen.
            if (locked) pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }

    bool try_receive(void* buffer, unsigned int buffer_size, unsigned int& message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (buffer_size < m_pHeader->m_MaxMessageSize) throw std::logic_error("Insufficient buffer");
        bool locked = false;
        try
        {
            int err = aux::pthread_mutex_lock(&m_pHeader->m_Mutex);
            locked = true;
            if (err == EOWNERDEAD)
            {
                clear_queue();
                aux::pthread_mutex_consistent(&m_pHeader->m_Mutex);
            }
            if (!m_pHeader->m_QueueSize)
            {
                aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
                return false;
            }
            get_message(buffer, buffer_size, message_size);
            aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            return true;
        }
        catch (...)
        {
            if (locked) aux::pthread_mutex_unlock(&m_pHeader->m_Mutex);
            throw;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
//  message_queue_type implementation
////////////////////////////////////////////////////////////////////////////////
template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::message_queue_type()
  : m_pImpl(new implementation())
{
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::message_queue_type(
  char const* name, open_mode mode, unsigned int max_queue_size, unsigned int max_message_size,
  permission const& permission_value)
  : m_pImpl(new implementation())
{
    open(name, mode, max_queue_size, max_message_size, permission_value);
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::~message_queue_type()
{
    delete m_pImpl;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::message_queue_type::message_queue_type(
  BOOST_RV_REF(message_queue_type) other)
  : m_pImpl(new implementation())
{
    swap(other);
}

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_queue_type&
basic_text_ipc_message_queue_backend< CharT >::message_queue_type::operator =(
  BOOST_RV_REF(message_queue_type) other)
{
    if (this != &other)
    {
        close();
        swap(other);
    }
    return *this;
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::swap(
  message_queue_type& other)
{
    std::swap(m_pImpl, other.m_pImpl);
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::open(
  char const* name, open_mode mode, unsigned int max_queue_size, unsigned int max_message_size,
  permission const& permission_value)
{
    if (mode != open_or_create)
    {
        return m_pImpl->open(name, mode, max_queue_size, max_message_size, permission_value);
    }
    else
    {
        while (true)
        {
            if (m_pImpl->open(name, create_only, max_queue_size, max_message_size, permission_value)) return true;
            if (m_pImpl->open(name, open_only, max_queue_size, max_message_size, permission_value)) return true;
            sched_yield();
        }
    }
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::is_open() const
{
    return m_pImpl->is_open();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::clear()
{
    m_pImpl->clear();
}

template < typename CharT >
BOOST_LOG_API std::string basic_text_ipc_message_queue_backend< CharT >::message_queue_type::name() const
{
    return m_pImpl->name();
}

template < typename CharT >
BOOST_LOG_API unsigned int basic_text_ipc_message_queue_backend< CharT >::message_queue_type::max_queue_size() const
{
    return m_pImpl->max_queue_size();
}

template < typename CharT >
BOOST_LOG_API unsigned int basic_text_ipc_message_queue_backend< CharT >::message_queue_type::max_message_size() const
{
    return m_pImpl->max_message_size();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::stop()
{
    m_pImpl->stop();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::reset()
{
    m_pImpl->reset();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::message_queue_type::close()
{
    m_pImpl->close();
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::send(
  void const* message_data, unsigned int message_size)
{
    return m_pImpl->send(message_data, message_size);
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::try_send(
  void const* message_data, unsigned int message_size)
{
    return m_pImpl->try_send(message_data, message_size);
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::receive(
  void* buffer, unsigned int buffer_size, unsigned int& message_size)
{
    return m_pImpl->receive(buffer, buffer_size, message_size);
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::message_queue_type::try_receive(
  void* buffer, unsigned int buffer_size, unsigned int& message_size)
{
    return m_pImpl->try_receive(buffer, buffer_size, message_size);
}

} // namespace sinks

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#endif // BOOST_WINDOWS

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_IPC_MESSAGE_QUEUE_POSIX_HPP_INCLUDED_
