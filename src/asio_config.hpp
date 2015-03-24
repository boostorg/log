/*
 *          Copyright Andrey Semashev 2007 - 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   asio_config.hpp
 * \author Andrey Semashev
 * \date   24.03.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#ifndef BOOST_LOG_ASIO_CONFIG_HPP_INCLUDED_
#define BOOST_LOG_ASIO_CONFIG_HPP_INCLUDED_

#include <boost/log/detail/config.hpp>

#if !defined(BOOST_LOG_NO_ASIO)

#if defined(BOOST_LOG_DLL)
// Make sure we have our local copy of Boost.ASIO with our configuration. This is to avoid possible ABI clashes with user's code,
// if it's compiled with other macros defined. For shared library build we also limit symbol visibility, where possible (see build/Jamfile.v2).
// Alas, for static library build there is no such protection, so user's config has to match the one of Boost.Log.
#undef BOOST_ASIO_SEPARATE_COMPILATION
#undef BOOST_ASIO_DYN_LINK
#define BOOST_ASIO_HEADER_ONLY 1
#endif

// Unless explicitly enabled, disable compiler-based TLS in Boost.ASIO. First, this ensures that the built Boost.Log binaries
// actually don't use compiler-based TLS unless allowed. Second, this is a temporary workaround for dynamic linking issue with
// Clang on Android described here: https://tracker.crystax.net/issues/912 (linked binaries refer to the missing __tls_get_addr symbol).
#if !defined(BOOST_LOG_USE_COMPILER_TLS) && !defined(BOOST_ASIO_DISABLE_THREAD_KEYWORD_EXTENSION)
#define BOOST_ASIO_DISABLE_THREAD_KEYWORD_EXTENSION
#endif

#endif // !defined(BOOST_LOG_NO_ASIO)

#endif // BOOST_LOG_ASIO_CONFIG_HPP_INCLUDED_
