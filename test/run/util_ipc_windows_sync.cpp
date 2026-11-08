/*
 * Copyright David Christle 2026.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * https://www.boost.org/LICENSE_1_0.txt)
 */

#include <boost/log/detail/config.hpp>

#if defined(BOOST_WINDOWS) && !defined(BOOST_LOG_WITHOUT_IPC)

#define BOOST_TEST_MODULE util_ipc_windows_sync
#include <boost/test/unit_test.hpp>
#include <windows.h>
#include <functional>
#include <string>
#include "windows/ipc_sync_wrappers.hpp"

namespace {

boost::winapi::ULONGLONG_ current_tick = 1u;
boost::winapi::HANDLE_ tracked_handle = NULL;
bool tracked_handle_closed = false;
bool retained_wait_in_progress = false;
bool fail_next_wait = false;
std::function< void(boost::winapi::HANDLE_) > before_wait;
std::function< void() > after_wait;

} // namespace

namespace boost {
namespace winapi {

ULONGLONG_ test_GetTickCount64()
{
    return current_tick;
}

DWORD_ test_GetTickCount()
{
    return static_cast< DWORD_ >(current_tick);
}

BOOL_ test_CloseHandle(HANDLE_ handle)
{
    if (handle == tracked_handle)
        tracked_handle_closed = true;
    return CloseHandle(handle);
}

DWORD_ test_WaitForMultipleObjects(DWORD_ count, const HANDLE_* handles, BOOL_ all, DWORD_ timeout)
{
    std::function< void(HANDLE_) > before;
    before.swap(before_wait);
    std::function< void() > after;
    after.swap(after_wait);
    if (before)
        before(handles[0]);

    if (fail_next_wait)
    {
        fail_next_wait = false;
        SetLastError(ERROR_ACCESS_DENIED);
        return WAIT_FAILED_;
    }

    // A closed handle may already name another object; never wait on it in the failing test.
    if (before && retained_wait_in_progress && handles[0] == tracked_handle && tracked_handle_closed)
        return WAIT_OBJECT_0_;

    // A lost notification should fail the test instead of blocking the test executable.
    const DWORD_ bounded_timeout = timeout < 5000u ? timeout : 5000u;
    const DWORD_ result = WaitForMultipleObjects(count, handles, all, bounded_timeout);
    BOOST_REQUIRE_MESSAGE(result != WAIT_TIMEOUT_, "Timed out waiting for an IPC test semaphore");
    if (after)
        after();
    return result;
}

} // namespace winapi

BOOST_LOG_OPEN_NAMESPACE
namespace aux {
extern const char test_hex_char_table[2][16] = {
    {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'},
    {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'}
};
} // namespace aux
BOOST_LOG_CLOSE_NAMESPACE
} // namespace boost

// Run the implementation with controlled wait scheduling and elapsed time.
#undef BOOST_LOG_WINDOWS_IPC_SYNC_WRAPPERS_HPP_INCLUDED_
#undef BOOST_LOG_WINDOWS_AUTO_HANDLE_HPP_INCLUDED_
#define interprocess_event test_interprocess_event
#define interprocess_semaphore test_interprocess_semaphore
#define interprocess_mutex test_interprocess_mutex
#define interprocess_condition_variable test_interprocess_condition_variable
#define tick_count_clock test_tick_count_clock
#define auto_handle test_auto_handle
#define g_hex_char_table test_hex_char_table
#define GetTickCount64 test_GetTickCount64
#define GetTickCount test_GetTickCount
#define CloseHandle test_CloseHandle
#define WaitForMultipleObjects test_WaitForMultipleObjects
#include "../../src/windows/ipc_sync_wrappers.cpp"
#undef WaitForMultipleObjects
#undef CloseHandle
#undef GetTickCount
#undef GetTickCount64
#undef g_hex_char_table
#undef auto_handle
#undef tick_count_clock
#undef interprocess_condition_variable
#undef interprocess_mutex
#undef interprocess_semaphore
#undef interprocess_event

namespace ipc_aux = boost::log::ipc::aux;

struct condition_variable_fixture
{
    ipc_aux::test_interprocess_mutex::shared_state mutex_state;
    ipc_aux::test_interprocess_condition_variable::shared_state condition_state;
    ipc_aux::test_interprocess_mutex mutex;
    ipc_aux::test_interprocess_condition_variable condition;
    ipc_aux::test_interprocess_event abort_event;
    ipc_aux::test_interprocess_event continue_event;

    condition_variable_fixture()
    {
        static unsigned int sequence = 0u;
        const std::wstring name = L"Local\\boost_log_ipc_sync_" +
            std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
        mutex.create((name + L"_mutex").c_str(), &mutex_state);
        condition.init(name.c_str(), &condition_state);
        abort_event.create(NULL, true);
        abort_event.set();
        continue_event.create(NULL, true);
        current_tick = 1u;
        tracked_handle = NULL;
        tracked_handle_closed = false;
        retained_wait_in_progress = false;
        fail_next_wait = false;
    }

    ~condition_variable_fixture()
    {
        tracked_handle = NULL;
        retained_wait_in_progress = false;
        before_wait = std::function< void(boost::winapi::HANDLE_) >();
        after_wait = std::function< void() >();
    }

    bool wait(boost::winapi::HANDLE_ abort_handle)
    {
        mutex.lock();
        ipc_aux::test_interprocess_mutex::optional_unlock lock(mutex);
        return condition.wait(lock, abort_handle);
    }

    void notify()
    {
        mutex.lock();
        ipc_aux::test_interprocess_mutex::auto_unlock lock(mutex);
        condition.notify_all();
    }

    void abandon_signalled_semaphore()
    {
        // Notification after cancellation leaves an unused semaphore credit.
        after_wait = [this]() { notify(); };
        BOOST_CHECK(!wait(abort_event.get_handle()));
    }

    void retire_stale_semaphores()
    {
        abandon_signalled_semaphore();
        current_tick += 3000u;
        abandon_signalled_semaphore();
    }
};

BOOST_FIXTURE_TEST_CASE(retain_semaphore_until_wait_returns, condition_variable_fixture)
{
    before_wait = [this](boost::winapi::HANDLE_ handle)
    {
        tracked_handle = handle;
        retained_wait_in_progress = true;
        notify();
        // The original waiter is paused after unlocking and before the kernel wait.
        retire_stale_semaphores();
    };

    BOOST_CHECK(wait(continue_event.get_handle()));
    retained_wait_in_progress = false;
    BOOST_CHECK(!tracked_handle_closed);
}

BOOST_FIXTURE_TEST_CASE(release_semaphore_after_cancelled_wait, condition_variable_fixture)
{
    before_wait = [](boost::winapi::HANDLE_ handle) { tracked_handle = handle; };
    abandon_signalled_semaphore();
    retire_stale_semaphores();
    BOOST_CHECK(tracked_handle_closed);
}

BOOST_FIXTURE_TEST_CASE(release_semaphore_after_failed_wait, condition_variable_fixture)
{
    before_wait = [](boost::winapi::HANDLE_ handle) { tracked_handle = handle; };
    fail_next_wait = true;
    BOOST_CHECK_THROW(wait(continue_event.get_handle()), boost::log::system_error);
    notify();
    retire_stale_semaphores();
    BOOST_CHECK(tracked_handle_closed);
}

#else

int main()
{
    return 0;
}

#endif
