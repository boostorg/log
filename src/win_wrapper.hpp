/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   win_wrapper.hpp
 * \author Lingxi Li
 * \date   14.11.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * This file provides checked Windows API.
 */

#ifndef BOOST_LOG_WIN_WRAPPER_HPP_INCLUDED_
#define BOOST_LOG_WIN_WRAPPER_HPP_INCLUDED_

#include <boost/system/system_error.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/detail/header.hpp>

#if defined(BOOST_WINDOWS)

#include <windows.h>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

namespace {

inline int get_last_error()
{
    return static_cast< int >(GetLastError());
}

inline system::system_error make_win_system_error(char const* what)
{
    return system::system_error(get_last_error(), system::system_category(), what);
}

inline void close_handle(HANDLE handle)
{
    if (!CloseHandle(handle)) throw make_win_system_error("CloseHandle");
}

inline void safe_close_handle(HANDLE& handle)
{
    if (handle)
    {
        close_handle(handle);
        handle = 0;
    }
}

inline DWORD wait_for_single_object(HANDLE handle, DWORD duration)
{
    DWORD wait_result = WaitForSingleObject(handle, duration);
    return wait_result != WAIT_FAILED ? wait_result :
        throw make_win_system_error("WaitForSingleObject");
}

inline DWORD wait_for_multiple_objects(DWORD num, HANDLE const* handles, BOOL all, DWORD duration)
{
    DWORD wait_result = WaitForMultipleObjects(num, handles, all, duration);
    return wait_result != WAIT_FAILED ? wait_result :
        throw make_win_system_error("WaitForMultipleObjects");
}

inline HANDLE create_mutex(SECURITY_ATTRIBUTES* psa, BOOL initial_owner, char const* name)
{
    HANDLE hmutex = CreateMutexA(psa, initial_owner, name);
    return hmutex ? hmutex : throw make_win_system_error("CreateMutex");
}

inline HANDLE open_mutex(DWORD access, BOOL inheritable, char const* name)
{
    HANDLE hmutex = OpenMutexA(access, inheritable, name);
    return hmutex ? hmutex : throw make_win_system_error("OpenMutex");
}

inline void release_mutex(HANDLE hmutex)
{
    if (!ReleaseMutex(hmutex)) throw make_win_system_error("ReleaseMutex");
}

inline HANDLE create_event(SECURITY_ATTRIBUTES* psa, BOOL manual, BOOL initial_state, char const* name)
{
    HANDLE hevent = CreateEventA(psa, manual, initial_state, name);
    return hevent ? hevent : throw make_win_system_error("CreateEvent");
}

inline HANDLE open_event(DWORD access, BOOL inheritable, char const* name)
{
    HANDLE hevent = OpenEventA(access, inheritable, name);
    return hevent ? hevent : throw make_win_system_error("OpenEvent");
}

inline void set_event(HANDLE hevent)
{
    if (!SetEvent(hevent)) throw make_win_system_error("SetEvent");
}

inline void reset_event(HANDLE hevent)
{
    if (!ResetEvent(hevent)) throw make_win_system_error("ResetEvent");
}

inline HANDLE create_file_mapping(
  HANDLE hfile,
  SECURITY_ATTRIBUTES* psa,
  DWORD protect,
  DWORD size_high,
  DWORD size_low,
  char const* name)
{
    HANDLE hfilemapping = CreateFileMappingA(hfile, psa, protect, size_high, size_low, name);
    return hfilemapping ? hfilemapping : throw make_win_system_error("CreateFileMapping");
}

inline HANDLE open_file_mapping(DWORD access, BOOL inheritable, char const* name)
{
    HANDLE hfilemapping = OpenFileMappingA(access, inheritable, name);
    return hfilemapping ? hfilemapping : throw make_win_system_error("OpenFileMapping");
}

inline void* map_view_of_file(
  HANDLE hfilemapping, 
  DWORD access, 
  DWORD offset_high, 
  DWORD offset_low,
  SIZE_T size)
{
    void* p_memory = MapViewOfFile(hfilemapping, access, offset_high, offset_low, size);
    return p_memory ? p_memory : throw make_win_system_error("MapViewOfFile");
}

inline void unmap_view_of_file(void const* p_memory)
{
    if (!UnmapViewOfFile(p_memory)) throw make_win_system_error("UnmapViewOfFile");
}

template < typename T > inline
void safe_unmap_view_of_file(T*& p)
{
    if (p)
    {
        unmap_view_of_file(p);
        p = NULL;
    }
}

} // unnamed namespace

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#endif // BOOST_WINDOWS

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_WIN_WRAPPER_HPP_INCLUDED_
