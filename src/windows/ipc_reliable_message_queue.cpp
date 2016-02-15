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
#include <limits>
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
#include <boost/detail/winapi/thread.hpp> // SwitchToThread
#include <boost/detail/winapi/character_code_conversion.hpp>
#include "winthread_wrappers.hpp"
#include "windows_shared_memory.hpp"
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

BOOST_LOG_ANONYMOUS_NAMESPACE {

//! Converts UTF-8 to UTF-16
inline std::wstring utf8_to_utf16(const char* str)
{
    std::size_t utf8_len = std::strlen(str);
    if (utf8_len == 0)
        return std::wstring();
    else if (BOOST_UNLIKELY(utf8_len > static_cast< std::size_t >((std::numeric_limits< int >::max)())))
        BOOST_LOG_THROW_DESCR(bad_alloc, "Multibyte string too long");

    int len = boost::detail::winapi::MultiByteToWideChar(boost::detail::winapi::CP_UTF8_, boost::detail::winapi::MB_ERR_INVALID_CHARS_, str, static_cast< int >(utf8_len), NULL, 0);
    if (BOOST_LIKELY(len > 0))
    {
        std::wstring wstr;
        wstr.resize(len);

        len = boost::detail::winapi::MultiByteToWideChar(boost::detail::winapi::CP_UTF8_, boost::detail::winapi::MB_ERR_INVALID_CHARS_, str, static_cast< int >(utf8_len), &wstr[0], len);
        if (BOOST_LIKELY(len > 0))
        {
            return wstr;
        }
    }

    BOOST_LOG_THROW_DESCR(conversion_error, "Failed to convert UTF-8 to UTF-16");
    BOOST_LOG_UNREACHABLE_RETURN(std::wstring());
}

} // namespace

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
        //! A flag indicating that the queue is constructed (i.e. the queue is constructed when the value is not 0).
        boost::atomic< uint32_t > m_initialized;
        //! Number of allocation blocks in the queue.
        const uint32_t m_capacity;
        //! Size of an allocation block, in bytes.
        const uint32_t m_block_size;
        //! Shared state of the mutex for protecting queue data structures.
        boost::log::ipc::aux::interprocess_mutex::shared_state m_mutex_state;
        //! Shared state of the condition variable used to block writers when the queue is full.
        boost::log::ipc::aux::interprocess_condition_variable::shared_state m_nonfull_queue_state;
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
            // Must be initialized last. m_initialized is zero-initialized initially.
            m_initialized.fetch_add(1u, boost::memory_order_release);
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
            BOOST_LOG_MIX_HEADER_MEMBER(m_initialized);
            BOOST_LOG_MIX_HEADER_MEMBER(m_capacity);
            BOOST_LOG_MIX_HEADER_MEMBER(m_block_size);
            BOOST_LOG_MIX_HEADER_MEMBER(m_mutex_state);
            BOOST_LOG_MIX_HEADER_MEMBER(m_nonfull_queue_state);
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
    //! Shared memory object and mapping
    boost::log::ipc::aux::windows_shared_memory m_shared_memory;
    //! Queue overflow handling policy
    const overflow_policy m_overflow_policy;
    //! The mask for selecting bits that constitute size values from 0 to (block_size - 1)
    uint32_t m_block_size_mask;
    //! The number of the bit set in block_size (i.e. log base 2 of block_size)
    uint32_t m_block_size_log2;

    //! Mutex for protecting queue data structures.
    boost::log::ipc::aux::interprocess_mutex m_mutex;
    //! Event used to block readers when the queue is empty.
    boost::log::ipc::aux::interprocess_event m_nonempty_queue;
    //! Condition variable used to block writers when the queue is full.
    boost::log::ipc::aux::interprocess_condition_variable m_nonfull_queue;
    //! The event indicates that stop has been requested
    boost::log::ipc::aux::auto_handle m_stop;

public:
    //! The constructor creates a new shared memory segment
    implementation
    (
        open_mode::create_only_tag,
        const char* name,
        uint32_t capacity,
        uint32_t block_size,
        permissions const& perms,
        overflow_policy oflow_policy
    ) :
        m_shared_memory(boost::interprocess::create_only, name, boost::interprocess::read_write, boost::interprocess::permissions(perms.get_native())),
        m_overflow_policy(oflow_policy),
        m_block_size_mask(0u),
        m_block_size_log2(0u),
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
        permissions const& perms,
        overflow_policy oflow_policy
    ) :
        m_shared_memory(boost::interprocess::open_or_create, name, boost::interprocess::read_write, boost::interprocess::permissions(perms.get_native())),
        m_overflow_policy(oflow_policy),
        m_block_size_mask(0u),
        m_block_size_log2(0u),
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
        char const* name,
        overflow_policy oflow_policy
    ) :
        m_shared_memory(boost::interprocess::open_only, name, boost::interprocess::read_write),
        m_region(),
        m_overflow_policy(oflow_policy),
        m_block_size_mask(0u),
        m_block_size_log2(0u),
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

    const char* name() const BOOST_NOEXCEPT
    {
        return m_shared_memory.get_name();
    }

    uint32_t capacity() const BOOST_NOEXCEPT
    {
        return get_header()->m_capacity;
    }

    uint32_t block_size() const BOOST_NOEXCEPT
    {
        return get_header()->m_block_size;
    }

    void reset()
    {
        m_stop = false;
    }

    operation_result send(void const* message_data, uint32_t message_size)
    {
        const uint32_t block_count = estimate_block_count(message_size);

        header* const hdr = get_header();

        if (BOOST_UNLIKELY(block_count > hdr->m_capacity))
            BOOST_THROW_EXCEPTION(logic_error("Message size exceeds the interprocess queue capacity"));

        if (m_stop)
            return aborted;

        lock_queue();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);

        while (true)
        {
            if (m_stop)
                return aborted;

            if ((hdr->m_capacity - hdr->m_size) >= block_count)
                break;

            if (BOOST_UNLIKELY(m_overflow_policy == throw_on_overflow))
                BOOST_THROW_EXCEPTION(capacity_limit_reached("Interprocess queue is full"));

            hdr->m_nonfull_queue.wait(hdr->m_mutex);
        }

        put_message(message_data, message_size, block_count);

        return succeeded;
    }

    bool try_send(void const* message_data, uint32_t message_size)
    {
        const uint32_t block_count = estimate_block_count(message_size);

        header* const hdr = get_header();

        if (BOOST_UNLIKELY(block_count > hdr->m_capacity))
            BOOST_THROW_EXCEPTION(logic_error("Message size exceeds the interprocess queue capacity"));

        if (m_stop)
            return false;

        lock_queue();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);

        if (m_stop)
            return false;

        if ((hdr->m_capacity - hdr->m_size) < block_count)
            return false;

        put_message(message_data, message_size, block_count);

        return true;
    }

    operation_result receive(receive_handler handler, void* state)
    {
        if (m_stop)
            return aborted;

        lock_queue();
        header* const hdr = get_header();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);

        while (true)
        {
            if (m_stop)
                return aborted;

            if (hdr->m_size > 0u)
                break;

            hdr->m_nonempty_queue.wait(hdr->m_mutex);
        }

        get_message(handler, state);

        return succeeded;
    }

    bool try_receive(receive_handler handler, void* state)
    {
        if (m_stop)
            return false;

        lock_queue();
        header* const hdr = get_header();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);

        if (hdr->m_size == 0u)
            return false;

        get_message(handler, state);

        return true;
    }

    void stop()
    {
        if (m_stop)
            return;

        lock_queue();
        header* const hdr = get_header();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);

        m_stop = true;

        hdr->m_nonempty_queue.notify_all();
        hdr->m_nonfull_queue.notify_all();
    }

    void clear()
    {
        lock_queue();
        header* const hdr = get_header();
        boost::log::ipc::aux::interprocess_mutex::auto_unlock unlock(hdr->m_mutex);
        clear_queue();
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

        init_block_size(block_size);
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
                boost::detail::winapi::SwitchToThread();
            }
        }

        BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: shared memory segment is not initialized by creator for too long");

    done:
        try
        {
            // Check that the queue layout matches the current process ABI
            if (hdr->m_abi_tag != header::get_abi_tag())
                BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: the queue ABI is incompatible");

            if (!boost::log::aux::is_power_of_2(hdr->m_block_size))
                BOOST_LOG_THROW_DESCR(setup_error, "Boost.Log interprocess message queue cannot be opened: the queue block size is not a power of 2");

            init_block_size(hdr->m_block_size);
        }
        catch (...)
        {
            close_region();
            throw;
        }
    }

    void close_region() BOOST_NOEXCEPT
    {
        header* const hdr = get_header();

        if (hdr->m_ref_count.fetch_sub(1u, boost::memory_order_acq_rel) == 1u)
        {
            boost::interprocess::shared_memory_object::remove(m_shared_memory.get_name());

            hdr->~header();

            boost::interprocess::mapped_region().swap(m_region);
            boost::interprocess::shared_memory_object().swap(m_shared_memory);

            m_block_size_mask = 0u;
            m_block_size_log2 = 0u;
        }
    }

    void init_block_size(uint32_t block_size)
    {
        m_block_size_mask = block_size - 1u;

        uint32_t block_size_log2 = 0u;
        if ((block_size & 0x0000ffff) == 0u)
        {
            block_size >>= 16u;
            block_size_log2 += 16u;
        }
        if ((block_size & 0x000000ff) == 0u)
        {
            block_size >>= 8u;
            block_size_log2 += 8u;
        }
        if ((block_size & 0x0000000f) == 0u)
        {
            block_size >>= 4u;
            block_size_log2 += 4u;
        }
        if ((block_size & 0x00000003) == 0u)
        {
            block_size >>= 2u;
            block_size_log2 += 2u;
        }
        if ((block_size & 0x00000001) == 0u)
        {
            ++block_size_log2;
        }
        m_block_size_log2 = block_size_log2;
    }

    void lock_queue()
    {
        header* const hdr = get_header();

#if defined(BOOST_LOG_HAS_PTHREAD_MUTEX_ROBUST)
        try
        {
#endif
            hdr->m_mutex.lock();
#if defined(BOOST_LOG_HAS_PTHREAD_MUTEX_ROBUST)
        }
        catch (boost::log::ipc::aux::lock_owner_dead&)
        {
            // The mutex is locked by the current thread, but the previous owner terminated without releasing the lock
            try
            {
                clear_queue();
                hdr->m_mutex.recover();
            }
            catch (...)
            {
                hdr->m_mutex.unlock();
                throw;
            }
        }
#endif
    }

    void clear_queue()
    {
        header* const hdr = get_header();
        hdr->m_size = 0u;
        hdr->m_put_pos = 0u;
        hdr->m_get_pos = 0u;
        hdr->m_nonfull_queue.notify_all();
    }

    //! Returns the number of allocation blocks that are required to store user's payload of the specified size
    uint32_t estimate_block_count(uint32_t size) const BOOST_NOEXCEPT
    {
        // ceil((size + get_header_overhead()) / block_size)
        return (size + block_header::get_header_overhead() + m_block_size_mask) >> m_block_size_log2;
    }

    //! Puts the message to the back of the queue
    void put_message(void const* message_data, uint32_t message_size, uint32_t block_count)
    {
        header* const hdr = get_header();

        const uint32_t capacity = hdr->m_capacity;
        const uint32_t block_size = hdr->m_block_size;
        uint32_t pos = hdr->m_put_pos;

        block_header* block = hdr->get_block(pos);
        block->m_size = message_size;

        uint32_t write_size = (std::min)((capacity - pos) * block_size - block_header::get_header_overhead(), message_size);
        std::memcpy(block->get_data(), message_data, write_size);

        pos += block_count;
        if (BOOST_UNLIKELY(pos >= capacity))
        {
            // Write the rest of the message at the beginning of the queue
            pos -= capacity;
            message_data = static_cast< const unsigned char* >(message_data) + write_size;
            write_size = message_size - write_size;
            if (write_size > 0u)
                std::memcpy(hdr->get_block(0u), message_data, write_size);
        }

        hdr->m_put_pos = pos;

        const uint32_t old_queue_size = hdr->m_size;
        hdr->m_size = old_queue_size + block_count;
        if (old_queue_size == 0u)
            hdr->m_nonempty_queue.notify_one();
    }

    //! Retrieves the next message and invokes the handler to store the message contents
    void get_message(receive_handler handler, void* state)
    {
        header* const hdr = get_header();

        const uint32_t capacity = hdr->m_capacity;
        const uint32_t block_size = hdr->m_block_size;
        uint32_t pos = hdr->m_get_pos;

        block_header* block = hdr->get_block(pos);
        uint32_t message_size = block->m_size;
        uint32_t block_count = estimate_block_count(message_size);

        BOOST_ASSERT(block_count <= hdr->m_size);

        uint32_t read_size = (std::min)((capacity - pos) * block_size - block_header::get_header_overhead(), message_size);
        handler(state, block->get_data(), read_size);

        pos += block_count;
        if (BOOST_UNLIKELY(pos >= capacity))
        {
            // Read the tail of the message
            pos -= capacity;
            read_size = message_size - read_size;
            if (read_size > 0u)
                handler(state, hdr->get_block(0u), read_size);
        }

        hdr->m_get_pos = pos;
        hdr->m_size -= block_count;

        hdr->m_nonfull_queue.notify_all();
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
