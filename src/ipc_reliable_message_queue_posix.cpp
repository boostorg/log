/*
 *                 Copyright Lingxi Li 2015.
 *              Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   ipc_reliable_message_queue_posix.cpp
 * \author Lingxi Li
 * \author Andrey Semashev
 * \date   17.11.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * This file provides an interprocess message queue implementation on POSIX platforms.
 */

#include <boost/log/detail/config.hpp>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <new>
#include <string>
#include <exception>
#include <stdexcept>
#include <algorithm>
#include <unistd.h>
#if defined(BOOST_HAS_SCHED_YIELD)
#include <sched.h>
#elif defined(BOOST_HAS_PTHREAD_YIELD)
#include <pthread.h>
#elif defined(BOOST_HAS_NANOSLEEP)
#include <time.h>
#endif
#include <boost/assert.hpp>
#include <boost/static_assert.hpp>
#include <boost/cstdint.hpp>
#include <boost/atomic/atomic.hpp>
#include <boost/atomic/capabilities.hpp>
#include <boost/throw_exception.hpp>
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
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/type_traits/declval.hpp>
#include "pthread_wrappers.hpp"
#include <boost/log/detail/header.hpp>

#if BOOST_ATOMIC_INT32_LOCK_FREE != 2
// 32-bit atomic ops are required to be able to place atomic<uint32_t> in the process-shared memory
#error Boost.Log: Native 32-bit atomic operations are required but not supported by Boost.Atomic on the target platform
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! 32-bit MurmurHash3 algorithm implementation (https://en.wikipedia.org/wiki/MurmurHash)
class murmur3
{
private:
    uint32_t m_state;
    uint32_t m_len;

    static BOOST_CONSTEXPR_OR_CONST uint32_t c1 = 0xcc9e2d51;
    static BOOST_CONSTEXPR_OR_CONST uint32_t c2 = 0x1b873593;
    static BOOST_CONSTEXPR_OR_CONST uint32_t r1 = 15;
    static BOOST_CONSTEXPR_OR_CONST uint32_t r2 = 13;
    static BOOST_CONSTEXPR_OR_CONST uint32_t m = 5;
    static BOOST_CONSTEXPR_OR_CONST uint32_t n = 0xe6546b64;

public:
    explicit BOOST_CONSTEXPR murmur3(uint32_t seed) BOOST_NOEXCEPT : m_state(seed), m_len(0u)
    {
    }

    //! Mixing stage of the 32-bit MurmurHash3 algorithm
    void mix(uint32_t value) BOOST_NOEXCEPT
    {
        value *= c1;
        value = (value << r1) | (value >> (32u - r1));
        value *= c2;

        uint32_t h = m_state ^ value;
        m_state = ((h << r2) | (h >> (32u - r2))) * m + n;
        m_len += 4u;
    }

    //! Finalization stage of the 32-bit MurmurHash3 algorithm
    uint32_t finalize() BOOST_NOEXCEPT
    {
        uint32_t h = m_state ^ m_len;
        h ^= h >> 16u;
        h *= 0x85ebca6bu;
        h ^= h >> 13u;
        h *= 0xc2b2ae35u;
        h ^= h >> 16u;
        m_state = h;
        return h;
    }
};

BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::c1;
BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::c2;
BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::r1;
BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::r2;
BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::m;
BOOST_CONSTEXPR_OR_CONST uint32_t murmur3::n;

//! Aligns the \a size argument up to me an integer multiple of \a alignment, which must be a power of 2.
inline BOOST_CONSTEXPR std::size_t align_size(std::size_t size, std::size_t alignment) BOOST_NOEXCEPT
{
    return (size + alignment - 1u) & ~(alignment - 1u);
}

} // namespace

} // namespace aux

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

        //! Returns a pointer to the element data
        void* get_data() const BOOST_NOEXCEPT
        {
            return const_cast< unsigned char* >(reinterpret_cast< const unsigned char* >(this)) + boost::log::aux::align_size(sizeof(block_header), data_alignment);
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
        uint8_t m_padding[BOOST_LOG_CPU_CACHE_LINE_SIZE - sizeof(uint32_t)];
        //! Reference counter. Also acts as a flag indicating that the queue is constructed (i.e. the queue is constructed when the counter is not 0).
        boost::atomic< uint32_t > m_ref_count;
        //! Number of allocation blocks in the queue.
        uint32_t m_capacity;
        //! Size of an allocation block, in bytes.
        uint32_t m_block_size;
        //! Mutex for protecting queue data structures.
        boost::log::aux::interprocess_mutex m_mutex;
        //! Condition variable used to block readers when the queue is empty.
        boost::log::aux::interprocess_condition_variable m_nonempty_queue;
        //! Condition variable used to block writers when the queue is full.
        boost::log::aux::interprocess_condition_variable m_nonfull_queue;
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
            // The members in this sequence must be enumerated in the same order as they are declared in the header structure.
            // The ABI tag is supposed change whenever a member changes size or alignment (we rely on the fact that pthread
            // structures are already ABI-stable, so we don't check their internals).

            header* p = NULL;
            boost::log::aux::murmur3 hash(abi_version);

            // We will use these constants to align pointers
            hash.mix(BOOST_LOG_CPU_CACHE_LINE_SIZE);
            hash.mix(block_header::data_alignment);

#define BOOST_LOG_MIX_HEADER_MEMBER(name)\
            hash.mix(static_cast< uint32_t >(sizeof(p->name)));\
            hash.mix(static_cast< uint32_t >(offsetof(header, name)))

            BOOST_LOG_MIX_HEADER_MEMBER(m_abi_tag);
            BOOST_LOG_MIX_HEADER_MEMBER(m_padding);
            BOOST_LOG_MIX_HEADER_MEMBER(m_ref_count);
            BOOST_LOG_MIX_HEADER_MEMBER(m_capacity);
            BOOST_LOG_MIX_HEADER_MEMBER(m_block_size);
            BOOST_LOG_MIX_HEADER_MEMBER(m_mutex);
            BOOST_LOG_MIX_HEADER_MEMBER(m_nonempty_queue);
            BOOST_LOG_MIX_HEADER_MEMBER(m_nonfull_queue);
            BOOST_LOG_MIX_HEADER_MEMBER(m_queue_size);
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
    };

private:
    boost::interprocess::shared_memory_object m_shared_memory;
    boost::interprocess::mapped_region m_region;
    boost::atomic< bool > m_stop;

public:
    //! The constructor creates a new shared memory segment
    implementation
    (
        open_mode::create_only_tag,
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms
    ) :
        m_shared_memory(boost::interprocess::create_only, name, boost::interprocess::read_write, boost::interprocess::permissions(perms.get_native())),
        m_region(),
        m_stop(false)
    {
        create_region(capacity, block_size);
    }

    //! The constructor creates a new shared memory segment or opens the existing one
    implementation
    (
        open_mode::open_or_create_tag,
        char const* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms
    ) :
        m_shared_memory(boost::interprocess::open_or_create, name, boost::interprocess::read_write, boost::interprocess::permissions(perms.get_native())),
        m_region(),
        m_stop(false)
    {
        boost::interprocess::offset_t shmem_size = 0;
        if (!m_shared_memory.get_size(shmem_size) || shmem_size == 0)
            create_region(capacity, block_size);
        else
            adopt_region(shmem_size);
    }

    //! The constructor opens the existing shared memory segment
    implementation
    (
        open_mode::open_only_tag,
        char const* name
    ) :
        m_shared_memory(boost::interprocess::open_only, name, boost::interprocess::read_write),
        m_region(),
        m_stop(false)
    {
        boost::interprocess::offset_t shmem_size = 0;
        if (!m_shared_memory.get_size(shmem_size))
            BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: shared memory segment not found");

        adopt_region(shmem_size);
    }

    ~implementation()
    {
        close_region();
    }

private:
    header* get_header() const BOOST_NOEXCEPT
    {
        return static_cast< header* >(m_region.get_address());
    }

    static std::size_t estimate_region_size(uint32_t capacity, uint32_t block_size) BOOST_NOEXCEPT
    {
        return boost::log::aux::align_size(sizeof(header), BOOST_LOG_CPU_CACHE_LINE_SIZE) + static_cast< std::size_t >(capacity) * static_cast< std::size_t >(block_size);
    }

    void create_region(uint32_t capacity, uint32_t block_size)
    {
        const std::size_t shmem_size = estimate_region_size(capacity, block_size);
        m_shared_memory.truncate(shmem_size);
        boost::interprocess::mapped_region(m_shared_memory, boost::interprocess::read_write, 0u, shmem_size).swap(m_region);

        new (m_region.get_address()) header(capacity, block_size);
    }

    void adopt_region(std::size_t shmem_size)
    {
        if (shmem_size < sizeof(header))
            BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: shared memory segment size too small");

        boost::interprocess::mapped_region(m_shared_memory, boost::interprocess::read_write, 0u, shmem_size).swap(m_region);

        // Wait until the mapped region becomes initialized
        header* const hdr = get_header();
        BOOST_CONSTEXPR_OR_CONST unsigned int wait_loops = 200u, spin_loops = 16u, spins = 16u;
        for (unsigned int i = 0; i < wait_loops; ++i)
        {
            uint32_t ref_count = hdr->m_ref_count.load(boost::memory_order_acquire);
            while (ref_count > 0u)
            {
                if (hdr->m_ref_count.compare_exchange_weak(ref_count, ref_count + 1u, boost::memory_order_acq_rel, boost::memory_order_acquire))
                    goto done;
            }

            if (i < spin_loops)
            {
                for (unsigned int j = 0; j < spins; ++j)
                {
                    boost::log::aux::pause();
                }
            }
            else
            {
#if defined(BOOST_HAS_SCHED_YIELD)
                sched_yield();
#elif defined(BOOST_HAS_PTHREAD_YIELD)
                pthread_yield();
#elif defined(BOOST_HAS_NANOSLEEP)
                timespec ts = {};
                ts.tv_sec = 0;
                ts.tv_nsec = 1000;
                nanosleep(&ts, NULL);
#else
                usleep(1);
#endif
            }
        }

        BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: shared memory segment is not initialized by creator for too long");

    done:
        // Check that the queue layout matches the current process ABI
        if (hdr->m_abi_tag != header::get_abi_tag())
        {
            close_region();
            BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: the queue ABI is incompatible");
        }
    }

    void close_region() BOOST_NOEXCEPT
    {
        header* const hdr = get_header();

        if (hdr->m_ref_count.fetch_sub(1u, boost::memory_order_release) == 1u)
        {
            boost::interprocess::shared_memory_object::remove(m_shared_memory.get_name());

            hdr->~header();

            boost::interprocess::mapped_region().swap(m_region);
            boost::interprocess::shared_memory_object().swap(m_shared_memory);
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
        uint8_t* p = reinterpret_cast< uint8_t* >(m_pHeader);
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
        uint8_t* p = reinterpret_cast< uint8_t* >(m_pHeader);
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

BOOST_LOG_API void reliable_message_queue::create(char const* name, uint32_t capacity, uint32_t block_size, permissions const& perms)
{
    BOOST_ASSERT(m_impl == NULL);
    try
    {
        m_impl = new implementation(open_mode::create_only, name, capacity, boost::log::aux::align_size(block_size, BOOST_LOG_CPU_CACHE_LINE_SIZE), perms);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(name);
        throw;
    }
}

BOOST_LOG_API void reliable_message_queue::open_or_create(char const* name, uint32_t capacity, uint32_t block_size, permissions const& perms)
{
    BOOST_ASSERT(m_impl == NULL);
    try
    {
        m_impl = new implementation(open_mode::open_or_create, name, capacity, boost::log::aux::align_size(block_size, BOOST_LOG_CPU_CACHE_LINE_SIZE), perms);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(name);
        throw;
    }
}

BOOST_LOG_API void reliable_message_queue::open(char const* name)
{
    BOOST_ASSERT(m_impl == NULL);
    try
    {
        m_impl = new implementation(open_mode::open_only, name);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(name);
        throw;
    }
}

BOOST_LOG_API void reliable_message_queue::clear()
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        m_impl->clear();
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API const char* reliable_message_queue::name() const
{
    BOOST_ASSERT(m_impl != NULL);
    return m_impl->name();
}

BOOST_LOG_API uint32_t reliable_message_queue::capacity() const
{
    BOOST_ASSERT(m_impl != NULL);
    return m_impl->capacity();
}

BOOST_LOG_API uint32_t reliable_message_queue::block_size() const
{
    BOOST_ASSERT(m_impl != NULL);
    return m_impl->block_size();
}

BOOST_LOG_API void reliable_message_queue::stop()
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        m_impl->stop();
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API void reliable_message_queue::reset()
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        m_impl->reset();
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API void reliable_message_queue::do_close() BOOST_NOEXCEPT
{
    delete m_impl;
    m_impl = NULL;
}

BOOST_LOG_API bool reliable_message_queue::send(void const* message_data, uint32_t message_size)
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        return m_impl->send(message_data, message_size);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API bool reliable_message_queue::try_send(void const* message_data, uint32_t message_size)
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        return m_impl->try_send(message_data, message_size);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API bool reliable_message_queue::receive(void* buffer, uint32_t buffer_size, uint32_t& message_size)
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        return m_impl->receive(buffer, buffer_size, message_size);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

BOOST_LOG_API bool reliable_message_queue::try_receive(void* buffer, uint32_t buffer_size, uint32_t& message_size)
{
    BOOST_ASSERT(m_impl != NULL);
    try
    {
        return m_impl->try_receive(buffer, buffer_size, message_size);
    }
    catch (boost::exception& e)
    {
        e << boost::log::resource_name_info(m_impl->name());
        throw;
    }
}

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>
