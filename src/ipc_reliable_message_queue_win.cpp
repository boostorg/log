/*
 *                Copyright Lingxi Li 2015.
 *             Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   ipc_reliable_message_queue_win.hpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   28.10.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * This file provides an interprocess message queue implementation on POSIX platforms.
 */

#include <boost/log/detail/config.hpp>
#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <boost/assert.hpp>
#include <boost/static_assert.hpp>
#include <boost/cstdint.hpp>
#include <boost/atomic/atomic.hpp>
#include <boost/atomic/capabilities.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/ipc/reliable_message_queue.hpp>
#include <boost/log/support/exception.hpp>
#include <boost/log/detail/pause.hpp>
#include <boost/exception/info.hpp>
#include <boost/exception/enable_error_info.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/permissions.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/windows_shared_memory.hpp>
#include "win_wrapper.hpp"
#include "murmur3.hpp"
#include "bit_tools.hpp"
#include <windows.h>
#include <boost/log/detail/header.hpp>

#if BOOST_ATOMIC_INT32_LOCK_FREE != 2
// 32-bit atomic ops are required to be able to place atomic<uint32_t> in the process-shared memory
#error Boost.Log: Native 32-bit atomic operations are required but not supported by Boost.Atomic on the target platform
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace ipc {

//! Message queue implementation data
struct reliable_message_queue::implementation
{
private:
    //! Header of an allocation block within the message queue. Placed at the beginning of the block within the shared memory segment.
    struct block_header
    {
        // Element data alignment, in bytes
        enum { data_alignment = 32u };

        //! Size of the element data, in bytes
        uint32_t m_size;

        //! Returns the block header overhead, in bytes
        static BOOST_CONSTEXPR uint32_t get_header_overhead() BOOST_NOEXCEPT
        {
            return boost::log::aux::align_size(sizeof(block_header), data_alignment);
        }

        //! Returns a pointer to the element data
        void* get_data() const BOOST_NOEXCEPT
        {
            return const_cast< unsigned char* >(reinterpret_cast< const unsigned char* >(this)) + get_header_overhead();
        }
    };

    //! Header of the message queue. Placed at the beginning of the shared memory segment.
    struct header
    {
        // Increment this constant whenever you change the binary layout of the queue (apart from this header structure)
        enum { abi_version = 0 };

        // !!! Whenever you add/remove members in this structure, also modify get_abi_tag() function accordingly !!!

        //! A tag value to ensure the correct binary layout of the message queue data structures. Must be placed first and always have a fixed size and alignment.
        uint32_t m_abi_tag;
        //! Padding to protect against alignment changes in Boost.Atomic. Don't use BOOST_ALIGNMENT to ensure portability.
        unsigned char m_padding[BOOST_LOG_CPU_CACHE_LINE_SIZE - sizeof(uint32_t)];
        //! Reference counter. Also acts as a flag indicating that the queue is constructed (i.e. the queue is constructed when the counter is not 0).
        boost::atomic< uint32_t > m_ref_count;
        //! Number of allocation blocks in the queue.
        const uint32_t m_capacity;
        //! Size of an allocation block, in bytes.
        const uint32_t m_block_size;
        //! The current number of allocated blocks in the queue.
        uint32_t m_size;
        //! The current writing position (allocation block index).
        uint32_t m_put_pos;
        //! The current reading position (allocation block index).
        uint32_t m_get_pos;

        header(uint32_t capacity, uint32_t block_size) :
            m_abi_tag(get_abi_tag()),
            m_capacity(capacity),
            m_block_size(block_size),
            m_size(0u),
            m_put_pos(0u),
            m_get_pos(0u)
        {
            // Must be initialized last. m_ref_count is zero-initialized initially.
            m_ref_count.fetch_add(1u, boost::memory_order_release);
        }

        //! Returns the header structure ABI tag
        static uint32_t get_abi_tag() BOOST_NOEXCEPT
        {
            // This FOURCC identifies the queue type
            boost::log::aux::murmur3_32 hash(boost::log::aux::make_fourcc('r', 'e', 'l', 'q'));

            // This FOURCC identifies the queue implementation
            hash.mix(boost::log::aux::make_fourcc('w', 'n', 't', '5'));
            hash.mix(abi_version);

            // We will use these constants to align pointers
            hash.mix(BOOST_LOG_CPU_CACHE_LINE_SIZE);
            hash.mix(block_header::data_alignment);

            // The members in the sequence below must be enumerated in the same order as they are declared in the header structure.
            // The ABI tag is supposed change whenever a member changes size or offset from the beginning of the header.

#define BOOST_LOG_MIX_HEADER_MEMBER(name)\
            hash.mix(static_cast< uint32_t >(sizeof(((header*)NULL)->name)));\
            hash.mix(static_cast< uint32_t >(offsetof(header, name)))

            BOOST_LOG_MIX_HEADER_MEMBER(m_abi_tag);
            BOOST_LOG_MIX_HEADER_MEMBER(m_padding);
            BOOST_LOG_MIX_HEADER_MEMBER(m_ref_count);
            BOOST_LOG_MIX_HEADER_MEMBER(m_capacity);
            BOOST_LOG_MIX_HEADER_MEMBER(m_block_size);
            BOOST_LOG_MIX_HEADER_MEMBER(m_size);
            BOOST_LOG_MIX_HEADER_MEMBER(m_put_pos);
            BOOST_LOG_MIX_HEADER_MEMBER(m_get_pos);

#undef BOOST_LOG_MIX_HEADER_MEMBER

            return hash.finalize();
        }

        //! Returns an element header at the specified index
        block_header* get_block(uint32_t index) const BOOST_NOEXCEPT
        {
            BOOST_ASSERT(index < m_capacity);
            unsigned char* p = const_cast< unsigned char* >(reinterpret_cast< const unsigned char* >(this)) + boost::log::aux::align_size(sizeof(header), BOOST_LOG_CPU_CACHE_LINE_SIZE);
            p += static_cast< std::size_t >(m_block_size) * static_cast< std::size_t >(index);
            return reinterpret_cast< block_header* >(p);
        }

        BOOST_DELETED_FUNCTION(header(header const&))
        BOOST_DELETED_FUNCTION(header& operator=(header const&))
    };

private:
    boost::interprocess::windows_shared_memory m_shared_memory;
    boost::interprocess::mapped_region m_region;
    const overflow_policy m_overflow_policy;
    uint32_t m_block_size_mask;
    uint32_t m_block_size_log2;
    bool m_stop;

    HANDLE  m_hStopEvent;
    std::string m_Name;
    HANDLE  m_hMutex;
    HANDLE  m_hFileMapping;
    header* m_pHeader;
    HANDLE  m_hNonEmptyQueueEvent;
    HANDLE  m_hNonFullQueueEvent;

    void clear_queue()
    {
        m_pHeader->m_QueueSize = 0;
        m_pHeader->m_PutPos = 0;
        m_pHeader->m_GetPos = 0;
        aux::set_event(m_hNonFullQueueEvent);
    }

    implementation()
      : m_hStopEvent(aux::create_event(NULL, TRUE, TRUE, NULL))
      , m_hMutex(0)
      , m_hFileMapping(0)
      , m_pHeader(NULL)
      , m_hNonEmptyQueueEvent(0)
      , m_hNonFullQueueEvent(0)
    {
    }

    ~implementation()
    {
        try
        {
            if (is_open()) close();
            aux::close_handle(m_hStopEvent);
        }
        catch (...)
        {
            std::terminate();
        }
    }

    void stop()
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        aux::set_event(m_hStopEvent);
    }

    void reset()
    {
        aux::reset_event(m_hStopEvent);
    }

    void close()
    {
        aux::safe_close_handle(m_hNonFullQueueEvent);
        aux::safe_close_handle(m_hNonEmptyQueueEvent);
        aux::safe_unmap_view_of_file(m_pHeader);
        aux::safe_close_handle(m_hFileMapping);
        aux::safe_close_handle(m_hMutex);
        m_Name.clear();
    }

    bool open(char const* name, open_mode mode, unsigned int max_queue_size,
      unsigned int max_message_size, permission const& permission_value)
    {
        if (is_open()) close();

        system::system_error sys_err(0, system::system_category());
        errno = 0;

        m_Name = name;
        if (*name)
        {
            unsigned int memory_size = sizeof(header) +
                (sizeof(unsigned int) + max_message_size) * max_queue_size;

            static char const uuid[] = "37394D1EBAC14602BC9492CB1971F756";
            std::string mutex_name = m_Name + uuid + "Mutex";
            std::string non_empty_event_name = m_Name + uuid + "NonEmptyQueueEvent";
            std::string non_full_event_name = m_Name + uuid + "NonFullQueueEvent";

            try
            {
                if (mode == open_or_create)
                {
                    SetLastError(ERROR_SUCCESS);
                    m_hMutex = aux::create_mutex(get_psa(permission_value), FALSE, mutex_name.c_str());
                    mode = GetLastError() == ERROR_ALREADY_EXISTS ? open_only : create_only;
                }

                if (mode == create_only)
                {
                    if (!m_hMutex)
                    {
                        SetLastError(ERROR_SUCCESS);
                        m_hMutex = aux::create_mutex(get_psa(permission_value), FALSE, mutex_name.c_str());
                        if (GetLastError() == ERROR_ALREADY_EXISTS)
                        {
                            throw aux::make_win_system_error("CreateMutex");
                        }
                    }

                    // Mutual exclusion begins.
                    m_hFileMapping = aux::create_file_mapping(INVALID_HANDLE_VALUE,
                        get_psa(permission_value), PAGE_READWRITE, 0, memory_size, name);
                    void* p_memory = aux::map_view_of_file(m_hFileMapping, FILE_MAP_WRITE, 0, 0, 0);
                    m_pHeader = static_cast< header* >(p_memory);

                    m_pHeader->m_MaxQueueSize = max_queue_size;
                    m_pHeader->m_MaxMessageSize = max_message_size;
                    m_pHeader->m_QueueSize = 0;
                    m_pHeader->m_PutPos = 0;
                    m_pHeader->m_GetPos = 0;

                    m_hNonEmptyQueueEvent = aux::create_event(get_psa(permission_value),
                        TRUE, TRUE, non_empty_event_name.c_str());
                    m_hNonFullQueueEvent = aux::create_event(get_psa(permission_value),
                        TRUE, TRUE, non_full_event_name.c_str());
                    // Mutual exclusion ends.

                    errno = ENOENT;
                }
                else // mode == open_only
                {
                    if (!m_hMutex)
                    {
                        m_hMutex = aux::open_mutex(SYNCHRONIZE, FALSE, mutex_name.c_str());
                    }

                    m_hFileMapping = aux::open_file_mapping(FILE_MAP_WRITE, FALSE, name);
                    void* p_memory = aux::map_view_of_file(m_hFileMapping, FILE_MAP_WRITE, 0, 0, 0);
                    m_pHeader = static_cast< header* >(p_memory);

                    m_hNonEmptyQueueEvent = aux::open_event(SYNCHRONIZE | EVENT_MODIFY_STATE,
                        FALSE, non_empty_event_name.c_str());
                    m_hNonFullQueueEvent = aux::open_event(SYNCHRONIZE | EVENT_MODIFY_STATE,
                        FALSE, non_full_event_name.c_str());

                    errno = EEXIST;
                }
            }
            catch (system::system_error const& except)
            {
                sys_err = except;
            }
            catch (...)
            {
                sys_err = system::system_error(ERROR_FUNCTION_FAILED, system::system_category(),
                    "message_queue_type::open");
            }
        }

        if (sys_err.code())
        {
            close();
            errno = sys_err.code().value() == ERROR_FILE_NOT_FOUND ? ENOENT :
                    sys_err.code().value() == ERROR_ALREADY_EXISTS ? EEXIST : 0;
            if (!errno) throw sys_err;
            return false;
        }
        else
        {
            reset();
            return true;
        }
    }

    bool is_open() const
    {
        return m_pHeader;
    }

    void clear()
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        mutex_type locker(m_hMutex);
        locker.lock();
        clear_queue();
    }

    std::string name() const
    {
        return m_Name;
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
        aux::set_event(m_hNonEmptyQueueEvent);
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
        aux::set_event(m_hNonFullQueueEvent);
    }

    bool send(void const* message_data, unsigned int message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (message_size > m_pHeader->m_MaxMessageSize) throw std::logic_error("Message is too long");
        errno = 0;
        mutex_type locker(m_hMutex);

        while (true)
        {
            if (locker.lock() == WAIT_ABANDONED) clear_queue();
            if (m_pHeader->m_QueueSize >= m_pHeader->m_MaxQueueSize) // full
            {
                aux::reset_event(m_hNonFullQueueEvent);
                locker.unlock();

                HANDLE handles[2] = { m_hStopEvent, m_hNonFullQueueEvent };
                DWORD wait_result = aux::wait_for_multiple_objects(2, handles, FALSE, INFINITE);
                if (wait_result == WAIT_OBJECT_0)
                {
                    errno = EINTR;
                    return false;
                }
            }
            else // not full
            {
                put_message(message_data, message_size);
                return true;
            }
        }
    }

    bool try_send(void const* message_data, unsigned int message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (message_size > m_pHeader->m_MaxMessageSize) throw std::logic_error("Message is too long");
        mutex_type locker(m_hMutex);
        if (locker.lock() == WAIT_ABANDONED) clear_queue();
        if (m_pHeader->m_QueueSize >= m_pHeader->m_MaxQueueSize) return false;
        put_message(message_data, message_size);
        return true;
    }

    bool receive(void* buffer, unsigned int buffer_size, unsigned int& message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (buffer_size < m_pHeader->m_MaxMessageSize) throw std::logic_error("Insufficient buffer");
        errno = 0;
        mutex_type locker(m_hMutex);

        while (true)
        {
            if (locker.lock() == WAIT_ABANDONED) clear_queue();
            if (!m_pHeader->m_QueueSize) // empty
            {
                aux::reset_event(m_hNonEmptyQueueEvent);
                locker.unlock();

                HANDLE handles[2] = { m_hStopEvent, m_hNonEmptyQueueEvent };
                DWORD wait_result = aux::wait_for_multiple_objects(2, handles, FALSE, INFINITE);
                if (wait_result == WAIT_OBJECT_0)
                {
                    errno = EINTR;
                    return false;
                }
            }
            else // nonempty
            {
                get_message(buffer, buffer_size, message_size);
                return true;
            }
        }
    }

    bool try_receive(void* buffer, unsigned int buffer_size, unsigned int& message_size)
    {
        if (!is_open()) throw std::logic_error("IPC message queue not opened");
        if (buffer_size < m_pHeader->m_MaxMessageSize) throw std::logic_error("Insufficient buffer");
        mutex_type locker(m_hMutex);
        if (locker.lock() == WAIT_ABANDONED) clear_queue();
        if (!m_pHeader->m_QueueSize) return false;
        get_message(buffer, buffer_size, message_size);
        return true;
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
    bool ok = false;
    while (true)
    {
        ok = m_pImpl->open(name, mode, max_queue_size, max_message_size, permission_value);
        if (!ok && mode == open_or_create && errno == ENOENT)
        {
            Sleep(0);
            continue;
        }
        break;
    }
    return ok;
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
    if (is_open()) m_pImpl->close();
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

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>
