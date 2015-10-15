/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   posix_wrapper.hpp
 * \author Lingxi Li
 * \date   17.11.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 *
 * This file provides checked POSIX.
 */

#ifndef BOOST_LOG_POSIX_WRAPPER_HPP_INCLUDED_
#define BOOST_LOG_POSIX_WRAPPER_HPP_INCLUDED_

#include <cerrno>
#include <cstddef>
#include <boost/system/system_error.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/detail/header.hpp>

#ifndef BOOST_WINDOWS

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

namespace {

inline system::error_code make_error_code(
  int value = errno,
  system::error_category const& category = system::system_category())
{
    return system::error_code(value, category);
}

inline system::system_error make_system_error(
  char const* api_name,
  int value = errno,
  system::error_category const& category = system::system_category())
{
    return system::system_error(value, category, api_name);
}

// general file operations
inline void close(int fd)
{
    if (::close(fd) != 0) throw make_system_error("close");
}

inline void safe_close(int& fd)
{
    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
}

inline void ftruncate(int fd, off_t size)
{
    if (::ftruncate(fd, size) != 0) throw make_system_error("ftruncate");
}

inline void fstat(int fd, struct stat* p_stat)
{
    if (::fstat(fd, p_stat) != 0) throw make_system_error("fstat");
}

// shared memory
inline int shm_open(char const* name, int oflag, mode_t permission)
{
    int shm_fd = ::shm_open(name, oflag, permission);
    return shm_fd >= 0 ? shm_fd : throw make_system_error("shm_open");
}

inline void shm_unlink(char const* name)
{
    if (::shm_unlink(name) != 0) throw make_system_error("shm_unlink");
}

inline void* mmap(
  void* addr, size_t size, int protection, int flag, int fd, off_t offset)
{
    void* memory = ::mmap(addr, size, protection, flag, fd, offset);
    return memory != MAP_FAILED ? memory : throw make_system_error("mmap");
}

template < typename Ptr >
inline Ptr typed_mmap(
  void* addr, size_t size, int protection, int flag, int fd, off_t offset)
{
    return static_cast<Ptr>(mmap(addr, size, protection, flag, fd, offset));
}

inline void munmap(void* memory, size_t size)
{
    if (::munmap(memory, size) != 0) throw make_system_error("munmap");
}

template < typename T >
inline void safe_munmap(T*& memory, size_t size)
{
    if (memory)
    {
        munmap(memory, size);
        memory = NULL;
    }
}

// pthread mutex
inline void pthread_mutexattr_init(pthread_mutexattr_t* p_attr)
{
    int err = ::pthread_mutexattr_init(p_attr);
    if (err != 0) throw make_system_error("pthread_mutexattr_init", err);
}

inline void pthread_mutexattr_destroy(pthread_mutexattr_t* p_attr)
{
    int err = ::pthread_mutexattr_destroy(p_attr);
    if (err != 0) throw make_system_error("pthread_mutexattr_destroy", err);
}

inline void pthread_mutexattr_setpshared(pthread_mutexattr_t* p_attr, int val)
{
    int err = ::pthread_mutexattr_setpshared(p_attr, val);
    if (err != 0) throw make_system_error("pthread_mutexattr_setpshared", err);
}

inline void pthread_mutexattr_setrobust(pthread_mutexattr_t* p_attr, int val)
{
    int err = ::pthread_mutexattr_setrobust(p_attr, val);
    if (err != 0) throw make_system_error("pthread_mutexattr_setrobust", err);
}

inline void pthread_mutexattr_settype(pthread_mutexattr_t* p_attr, int val)
{
    int err = ::pthread_mutexattr_settype(p_attr, val);
    if (err != 0) throw make_system_error("pthread_mutexattr_settype", err);
}

inline void pthread_mutex_init(
  pthread_mutex_t* p_mutex, pthread_mutexattr_t const* p_attr)
{
    int err = ::pthread_mutex_init(p_mutex, p_attr);
    if (err != 0) throw make_system_error("pthread_mutex_init", err);
}

inline void pthread_mutex_destroy(pthread_mutex_t* p_mutex)
{
    int err = ::pthread_mutex_destroy(p_mutex);
    if (err != 0) throw make_system_error("pthread_mutex_destroy", err);
}

inline void pthread_mutex_consistent(pthread_mutex_t* p_mutex)
{
    int err = ::pthread_mutex_consistent(p_mutex);
    if (err != 0) throw make_system_error("pthread_mutex_consistent", err);
}

inline int pthread_mutex_lock(pthread_mutex_t* p_mutex)
{
    int err = ::pthread_mutex_lock(p_mutex);
    return (err == 0 || err == EOWNERDEAD) ? err :
        throw make_system_error("pthread_mutex_lock", err);
}

inline void pthread_mutex_unlock(pthread_mutex_t* p_mutex)
{
    int err = ::pthread_mutex_unlock(p_mutex);
    if (err != 0) throw make_system_error("pthread_mutex_unlock", err);
}

// pthread condition variable
inline void pthread_condattr_init(pthread_condattr_t* p_attr)
{
    int err = ::pthread_condattr_init(p_attr);
    if (err != 0) throw make_system_error("pthread_condattr_init", err);
}

inline void pthread_condattr_destroy(pthread_condattr_t* p_attr)
{
    int err = ::pthread_condattr_destroy(p_attr);
    if (err != 0) throw make_system_error("pthread_condattr_destroy", err);
}

inline void pthread_condattr_setpshared(pthread_condattr_t* p_attr, int val)
{
    int err = ::pthread_condattr_setpshared(p_attr, val);
    if (err != 0) throw make_system_error("pthread_condattr_setpshared", err);
}

inline void pthread_cond_init(
  pthread_cond_t* p_cond, pthread_condattr_t const* p_attr)
{
    int err = ::pthread_cond_init(p_cond, p_attr);
    if (err != 0) throw make_system_error("pthread_cond_init", err);
}

inline void pthread_cond_destroy(pthread_cond_t* p_cond)
{
    int err = ::pthread_cond_destroy(p_cond);
    if (err != 0) throw make_system_error("pthread_cond_destroy", err);
}

inline int pthread_cond_wait(pthread_cond_t* p_cond, pthread_mutex_t* p_mutex)
{
    int err = ::pthread_cond_wait(p_cond, p_mutex);
    return (err == 0 || err == EOWNERDEAD) ? err :
        throw make_system_error("pthread_cond_wait", err);
}

inline void pthread_cond_signal(pthread_cond_t* p_cond)
{
    int err = ::pthread_cond_signal(p_cond);
    if (err != 0) throw make_system_error("pthread_cond_signal", err);
}

inline void pthread_cond_broadcast(pthread_cond_t* p_cond)
{
    int err = ::pthread_cond_broadcast(p_cond);
    if (err != 0) throw make_system_error("pthread_cond_broadcast", err);
}

} // unnamed namespace

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#endif // BOOST_WINDOWS

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_POSIX_WRAPPER_HPP_INCLUDED_
