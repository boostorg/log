/*
 *             Copyright Andrey Semashev 2026.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          https://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sinks/queue_unblocking_flags.hpp
 * \author Andrey Semashev
 * \date   12.11.2026
 *
 * The header contains definition of log record queue unblocking flags.
 */

#ifndef BOOST_LOG_SINKS_QUEUE_UNBLOCKING_FLAGS_HPP_INCLUDED_HPP_
#define BOOST_LOG_SINKS_QUEUE_UNBLOCKING_FLAGS_HPP_INCLUDED_HPP_

#include <boost/log/detail/config.hpp>
#include <boost/log/detail/header.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace sinks {

//! The enum defines flags for unblocking log record queue
enum class queue_unblocking_flags : unsigned int
{
    none = 0u,
    dequeue_one = 1u,            //!< Unblock one thread blocked in a dequeue operation
    enqueue_all = (1u << 1u)     //!< Unblock all threads blocked in an enqueue operation
};

inline constexpr queue_unblocking_flags operator| (queue_unblocking_flags left, queue_unblocking_flags right) noexcept
{
    return static_cast< queue_unblocking_flags >(static_cast< unsigned int >(left) | static_cast< unsigned int >(right));
}

inline BOOST_CXX14_CONSTEXPR queue_unblocking_flags& operator|= (queue_unblocking_flags& left, queue_unblocking_flags right) noexcept
{
    left = left | right;
    return left;
}

inline constexpr queue_unblocking_flags operator& (queue_unblocking_flags left, queue_unblocking_flags right) noexcept
{
    return static_cast< queue_unblocking_flags >(static_cast< unsigned int >(left) & static_cast< unsigned int >(right));
}

inline BOOST_CXX14_CONSTEXPR queue_unblocking_flags& operator&= (queue_unblocking_flags& left, queue_unblocking_flags right) noexcept
{
    left = left & right;
    return left;
}

inline constexpr queue_unblocking_flags operator^ (queue_unblocking_flags left, queue_unblocking_flags right) noexcept
{
    return static_cast< queue_unblocking_flags >(static_cast< unsigned int >(left) ^ static_cast< unsigned int >(right));
}

inline BOOST_CXX14_CONSTEXPR queue_unblocking_flags& operator^= (queue_unblocking_flags& left, queue_unblocking_flags right) noexcept
{
    left = left ^ right;
    return left;
}

} // namespace sinks

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_SINKS_AUTO_NEWLINE_MODE_HPP_INCLUDED_HPP_
