/*
 *              Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   winthread_wrappers.hpp
 * \author Andrey Semashev
 * \date   23.01.2016
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#ifndef BOOST_LOG_WINTHREAD_WRAPPERS_HPP_INCLUDED_
#define BOOST_LOG_WINTHREAD_WRAPPERS_HPP_INCLUDED_

#include <boost/log/detail/config.hpp>
#include <boost/detail/winapi/access_rights.hpp>
#include <boost/detail/winapi/handles.hpp>
#include <boost/detail/winapi/event.hpp>
#include <boost/detail/winapi/semaphore.hpp>
#include <boost/detail/winapi/wait.hpp>
#include <boost/detail/winapi/GetLastError.hpp>
#include <windows.h> // for error codes
#include <cstddef>
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <boost/memory_order.hpp>
#include <boost/atomic/atomic.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/permissions.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace ipc {

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

// TODO: Port to Boost.Atomic when it supports extended atomic ops
#if defined(BOOST_MSVC) && (_MSC_VER >= 1400) && !defined(UNDER_CE)

#if _MSC_VER == 1400
extern "C" unsigned char _interlockedbittestandset(long *a, long b);
extern "C" unsigned char _interlockedbittestandreset(long *a, long b);
#else
extern "C" unsigned char _interlockedbittestandset(volatile long *a, long b);
extern "C" unsigned char _interlockedbittestandreset(volatile long *a, long b);
#endif

#pragma intrinsic(_interlockedbittestandset)
#pragma intrinsic(_interlockedbittestandreset)

BOOST_FORCEINLINE bool bit_test_and_set(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    return _interlockedbittestandset(reinterpret_cast< long* >(&x.storage()), static_cast< long >(bit)) != 0;
}

BOOST_FORCEINLINE bool bit_test_and_reset(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    return _interlockedbittestandreset(reinterpret_cast< long* >(&x.storage()), static_cast< long >(bit)) != 0;
}

#elif (defined(BOOST_MSVC) || defined(BOOST_INTEL_WIN)) && defined(_M_IX86)

BOOST_FORCEINLINE bool bit_test_and_set(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    boost::atomic< uint32_t >::storage_type* p = &x.storage();
    bool ret;
    __asm
    {
        mov eax, bit
        mov edx, p
        lock bts [edx], eax
        setc ret
    };
    return ret;
}

BOOST_FORCEINLINE bool bit_test_and_reset(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    boost::atomic< uint32_t >::storage_type* p = &x.storage();
    bool ret;
    __asm
    {
        mov eax, bit
        mov edx, p
        lock btr [edx], eax
        setc ret
    };
    return ret;
}

#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))

#if !defined(__CUDACC__)
#define BOOST_LOG_DETAIL_ASM_CLOBBER_CC_COMMA "cc",
#else
#define BOOST_LOG_DETAIL_ASM_CLOBBER_CC_COMMA
#endif

BOOST_FORCEINLINE bool bit_test_and_set(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    bool res;
    __asm__ __volatile__
    (
        "lock; bts %[bit_number], %[storage]\n\t"
        "setc %[result]\n\t"
        : [storage] "+m" (x.storage()), [result] "=q" (res)
        : [bit_number] "Kq" (bit)
        : BOOST_LOG_DETAIL_ASM_CLOBBER_CC_COMMA "memory"
    );
    return res;
}

BOOST_FORCEINLINE bool bit_test_and_reset(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    bool res;
    __asm__ __volatile__
    (
        "lock; btr %[bit_number], %[storage]\n\t"
        "setc %[result]\n\t"
        : [storage] "+m" (x.storage()), [result] "=q" (res)
        : [bit_number] "Kq" (bit)
        : BOOST_LOG_DETAIL_ASM_CLOBBER_CC_COMMA "memory"
    );
    return res;
}

#else

BOOST_FORCEINLINE bool bit_test_and_set(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    const uint32_t mask = uint32_t(1u) << bit;
    uint32_t old_val = x.fetch_or(mask, boost::memory_order_acq_rel);
    return (old_val & mask) != 0u;
}

BOOST_FORCEINLINE bool bit_test_and_reset(boost::atomic< uint32_t >& x, uint32_t bit) BOOST_NOEXCEPT
{
    const uint32_t mask = uint32_t(1u) << bit;
    uint32_t old_val = x.fetch_and(~mask, boost::memory_order_acq_rel);
    return (old_val & mask) != 0u;
}

#endif


class auto_handle
{
private:
    boost::detail::winapi::HANDLE_ m_handle;

public:
    explicit auto_handle(boost::detail::winapi::HANDLE_ h = NULL) BOOST_NOEXCEPT : m_handle(h)
    {
    }

    ~auto_handle() BOOST_NOEXCEPT
    {
        if (m_handle)
            BOOST_VERIFY(boost::detail::winapi::CloseHandle(m_handle) != 0);
    }

    void init(boost::detail::winapi::HANDLE_ h) BOOST_NOEXCEPT
    {
        BOOST_ASSERT(m_handle == NULL);
        m_handle = h;
    }

    boost::detail::winapi::HANDLE_ get() const BOOST_NOEXCEPT { return m_handle; }

    BOOST_DELETED_FUNCTION(auto_handle(auto_handle const&))
    BOOST_DELETED_FUNCTION(auto_handle& operator=(auto_handle const&))
};

//! Interprocess mutex. Implementation adopted from Boost.Sync.
class interprocess_mutex
{
public:
    //! Shared state that should be visible to all processes using the mutex
    struct shared_state
    {
        boost::atomic< uint32_t > m_lock_state;

        shared_state() BOOST_NOEXCEPT : m_lock_state(0u)
        {
        }
    };

    struct auto_unlock
    {
        explicit auto_unlock(interprocess_mutex& mutex) BOOST_NOEXCEPT : m_mutex(mutex) {}
        ~auto_unlock() { m_mutex.unlock(); }

        BOOST_DELETED_FUNCTION(auto_unlock(auto_unlock const&))
        BOOST_DELETED_FUNCTION(auto_unlock& operator=(auto_unlock const&))

    private:
        interprocess_mutex& m_mutex;
    };

private:
    auto_handle m_event;
    shared_state* m_shared_state;

    static BOOST_CONSTEXPR_OR_CONST uint32_t lock_flag_bit = 31u;
    static BOOST_CONSTEXPR_OR_CONST uint32_t event_set_flag_bit = 30u;
    static BOOST_CONSTEXPR_OR_CONST uint32_t lock_flag_value = 1u << lock_flag_bit;
    static BOOST_CONSTEXPR_OR_CONST uint32_t event_set_flag_value = 1u << event_set_flag_bit;
    static BOOST_CONSTEXPR_OR_CONST uint32_t waiter_count_mask = event_set_flag_value - 1u;

public:
    interprocess_mutex() BOOST_NOEXCEPT : m_shared_state(NULL)
    {
    }

    void init(const wchar_t* name, shared_state* shared, permissions const& perms = permissions())
    {
#if BOOST_USE_WINAPI_VERSION >= BOOST_WINAPI_VERSION_WIN6
        boost::detail::winapi::HANDLE_ h = boost::detail::winapi::CreateEventExW(reinterpret_cast< boost::detail::winapi::SECURITY_ATTRIBUTES_* >(perms.get_native()), name, 0u, boost::detail::winapi::SYNCHRONIZE_ | boost::detail::winapi::EVENT_MODIFY_STATE_);
#else
        boost::detail::winapi::HANDLE_ h = boost::detail::winapi::CreateEventW(reinterpret_cast< boost::detail::winapi::SECURITY_ATTRIBUTES_* >(perms.get_native()), false, false, name);
#endif
        if (h == NULL)
        {
            boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
            if (BOOST_LIKELY(err == ERROR_ALREADY_EXISTS))
            {
                h = boost::detail::winapi::OpenEventW(boost::detail::winapi::SYNCHRONIZE_ | boost::detail::winapi::EVENT_MODIFY_STATE_, false, name);
                if (BOOST_UNLIKELY(h == NULL))
                {
                    err = boost::detail::winapi::GetLastError();
                    BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to open an event object for an interprocess mutex", (err));
                }
            }
            else
            {
                BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to create an event object for an interprocess mutex", (err));
            }
        }

        m_event.init(h);
        m_shared_state = shared;
    }

    bool try_lock()
    {
        return !bit_test_and_set(m_shared_state->m_lock_state, lock_flag_bit);
    }

    bool lock(boost::detail::winapi::HANDLE_ abort_handle)
    {
        if (BOOST_LIKELY(try_lock()))
            return true;
        return lock_slow(abort_handle);
    }

    void unlock() BOOST_NOEXCEPT
    {
        const uint32_t old_count = m_shared_state->m_lock_state.fetch_add(lock_flag_value, boost::memory_order_release);
        if ((old_count & event_set_flag_value) == 0u && (old_count > lock_flag_value))
        {
            if (!bit_test_and_set(m_shared_state->m_lock_state, event_set_flag_bit))
            {
                boost::detail::winapi::SetEvent(m_event.get());
            }
        }
    }

    BOOST_DELETED_FUNCTION(interprocess_mutex(interprocess_mutex const&))
    BOOST_DELETED_FUNCTION(interprocess_mutex& operator=(interprocess_mutex const&))

private:
    bool lock_slow(boost::detail::winapi::HANDLE_ abort_handle)
    {
        uint32_t old_state = m_shared_state->m_lock_state.load(boost::memory_order_relaxed);
        mark_waiting_and_try_lock(old_state);

        if ((old_state & lock_flag_value) != 0u) try
        {
            boost::detail::winapi::HANDLE_ handles[2u] = { m_event.get(), abort_handle };
            do
            {
                const boost::detail::winapi::DWORD_ retval = boost::detail::winapi::WaitForMultipleObjects(2u, handles, false, boost::detail::winapi::infinite);
                if (retval == (boost::detail::winapi::wait_object_0 + 1u))
                {
                    // Wait was interrupted
                    m_shared_state->m_lock_state.fetch_sub(1u, boost::memory_order_acq_rel);
                    return false;
                }
                else if (BOOST_UNLIKELY(retval != boost::detail::winapi::wait_object_0))
                {
                    const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
                    BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to block on the interprocess mutex", (err));
                }
                clear_waiting_and_try_lock(old_state);
            }
            while ((old_state & lock_flag_value) != 0u);
        }
        catch (...)
        {
            m_shared_state->m_lock_state.fetch_sub(1u, boost::memory_order_acq_rel);
            throw;
        }

        return true;
    }

    void mark_waiting_and_try_lock(uint32_t& old_state)
    {
        uint32_t was_locked, new_state;
        do
        {
            was_locked = (old_state & lock_flag_value);
            if (was_locked)
            {
                // Avoid integer overflows
                if (BOOST_UNLIKELY((old_state & waiter_count_mask) == waiter_count_mask))
                    BOOST_LOG_THROW_DESCR(limitation_error, "Too many waiters on an interprocess mutex");

                new_state = old_state + 1u;
            }
            else
            {
                new_state = old_state | lock_flag_value;
            }
        }
        while (!m_shared_state->m_lock_state.compare_exchange_weak(old_state, new_state, boost::memory_order_acq_rel, boost::memory_order_relaxed));
    }

    void clear_waiting_and_try_lock(uint32_t& old_state)
    {
        old_state &= ~lock_flag_value;
        old_state |= event_set_flag_value;
        uint32_t new_state;
        do
        {
            new_state = ((old_state & lock_flag_value) ? old_state : ((old_state - 1u) | lock_flag_value)) & ~event_set_flag_value;
        }
        while (!m_shared_state->m_lock_state.compare_exchange_strong(old_state, new_state, boost::memory_order_acq_rel, boost::memory_order_relaxed));
    }
};

//! Interprocess condition variable wrapper
struct interprocess_condition_variable
{
    pthread_cond_t cond;

    interprocess_condition_variable()
    {
        pthread_condition_variable_attributes attrs;
        int err = pthread_condattr_setpshared(&attrs.attrs, PTHREAD_PROCESS_SHARED);
        if (BOOST_UNLIKELY(err != 0))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to make pthread condition variable process-shared", (err));

        err = pthread_cond_init(&this->cond, &attrs.attrs);
        if (BOOST_UNLIKELY(err != 0))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to initialize pthread condition variable", (err));
    }

    ~interprocess_condition_variable()
    {
        BOOST_VERIFY(pthread_cond_destroy(&this->cond) == 0);
    }

    void notify_one()
    {
        int err = pthread_cond_signal(&this->cond);
        if (BOOST_UNLIKELY(err != 0))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to notify one thread on a pthread condition variable", (err));
    }

    void notify_all()
    {
        int err = pthread_cond_broadcast(&this->cond);
        if (BOOST_UNLIKELY(err != 0))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to notify all threads on a pthread condition variable", (err));
    }

    void wait(interprocess_mutex& mutex)
    {
        int err = pthread_cond_wait(&this->cond, &mutex.mutex);
        if (BOOST_UNLIKELY(err != 0))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to wait on a pthread condition variable", (err));
    }

    BOOST_DELETED_FUNCTION(interprocess_condition_variable(interprocess_condition_variable const&))
    BOOST_DELETED_FUNCTION(interprocess_condition_variable& operator=(interprocess_condition_variable const&))
};

} // namespace

} // namespace aux

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_WINTHREAD_WRAPPERS_HPP_INCLUDED_
