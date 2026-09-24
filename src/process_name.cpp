/*
 *          Copyright Andrey Semashev 2007 - 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   process_name.cpp
 * \author Andrey Semashev
 * \date   29.07.2012
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * The code in this file is based on information on this page:
 *
 * http://stackoverflow.com/questions/1023306/finding-current-executables-path-without-proc-self-exe
 */

#include <boost/log/detail/config.hpp>
#include <climits> // PATH_MAX
#include <boost/log/attributes/current_process_name.hpp>
#include <boost/filesystem/path.hpp>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#if defined(BOOST_WINDOWS)

#include <windows.h>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! The function returns the current process name
BOOST_LOG_API std::string get_process_name()
{
    std::wstring buf;
    buf.resize(PATH_MAX);
    do
    {
        unsigned int len = GetModuleFileNameW(nullptr, &buf[0], static_cast< unsigned int >(buf.size()));
        if (len < buf.size())
        {
            buf.resize(len);
            break;
        }

        buf.resize(buf.size() * 2u);
    }
    while (buf.size() < 65536u);

    return filesystem::path(buf).filename().string();
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)

#include <cstring>
#include <mach-o/dyld.h>
#include <boost/cstdint.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! The function returns the current process name
BOOST_LOG_API std::string get_process_name()
{
    std::string buf;
    buf.resize(PATH_MAX);
    while (true)
    {
        uint32_t size = static_cast< uint32_t >(buf.size());
        if (_NSGetExecutablePath(&buf[0], &size) == 0)
        {
            buf.resize(std::strlen(&buf[0]));
            break;
        }

        buf.resize(size);
    }

    return filesystem::path(buf).filename().string();
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#elif defined(__FreeBSD__)

#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <string>
#include <boost/filesystem/operations.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! The function returns the current process name
BOOST_LOG_API std::string get_process_name()
{
#if defined(KERN_PROC_PATHNAME)
    int mib[4u] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    char buf[PATH_MAX] = {};
    size_t cb = sizeof(buf);
    if (sysctl(mib, sizeof(mib) / sizeof(*mib), buf, &cb, nullptr, 0u) == 0)
        return filesystem::path(buf).filename().string();
#endif

    if (filesystem::exists("/proc/curproc/file"))
        return filesystem::read_symlink("/proc/curproc/file").filename().string();

    return std::to_string(getpid());
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#elif defined(__OpenBSD__)

#include <unistd.h>
#include <sys/param.h> // OpenBSD macro
#if (OpenBSD < 202610)
#include <stddef.h>
#include <sys/sysctl.h>
#if (OpenBSD <= 201411)
#include <boost/filesystem/operations.hpp>
#endif
#endif
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! The function returns the current process name
BOOST_LOG_API std::string get_process_name()
{
#if (OpenBSD >= 202610)
    // OpenBSD 8.0 added getexecpath
    char buf[PATH_MAX];

    if (getexecpath(buf, sizeof(buf)) == 0)
        return filesystem::path(buf).filename().string();
#else // (OpenBSD >= 202610)
#if (OpenBSD <= 201411)
    // OpenBSD 5.6 was the last to support /proc filesystem.
    // We still prefer it over sysctl(KERN_PROC) as the latter truncates executable name to 16 chars.
    if (filesystem::exists("/proc/curproc/file"))
        return filesystem::read_symlink("/proc/curproc/file").filename().string();
#endif // (OpenBSD <= 201411)
    // Note that we intentionally don't use getprogname() here as it is relying
    // on argv, which may be spoofed and not reflect the actual executable
    // name. Also, its usage is not thread-safe, as other threads may
    // concurrently call setprogname() and modify or free the previous buffer
    // that was used by getprogname(). This could happen concurrently with
    // get_process_name() using the buffer returned by getprogname().
    int mib[6u] =
    {
        CTL_KERN,
        KERN_PROC,
        KERN_PROC_PID,
        static_cast< int >(getpid()),
        static_cast< int >(sizeof(kinfo_proc)),
        1 // number of kinfo_proc structures to return
    };
    kinfo_proc kp;
    size_t len = sizeof(kp);

    if (sysctl(mib, sizeof(mib) / sizeof(*mib), &kp, &len, nullptr, 0u) == 0)
        return kp.p_comm;
#endif // (OpenBSD >= 202610)

    return std::to_string(getpid());
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#else

#include <unistd.h>
#include <string>
#include <boost/filesystem/operations.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! The function returns the current process name
BOOST_LOG_API std::string get_process_name()
{
    if (filesystem::exists("/proc/self/exe"))
        return filesystem::read_symlink("/proc/self/exe").filename().string();

    if (filesystem::exists("/proc/curproc/file"))
        return filesystem::read_symlink("/proc/curproc/file").filename().string();

    if (filesystem::exists("/proc/curproc/exe"))
        return filesystem::read_symlink("/proc/curproc/exe").filename().string();

    return std::to_string(getpid());
}

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif
