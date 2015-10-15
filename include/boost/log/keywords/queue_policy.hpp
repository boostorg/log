/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   keywords/queue_policy.hpp
 * \author Lingxi Li
 * \date   16.10.2015
 *
 * The header contains the \c queue_policy keyword declaration.
 */

#ifndef BOOST_LOG_KEYWORDS_QUEUE_POLICY_HPP_INCLUDED_
#define BOOST_LOG_KEYWORDS_QUEUE_POLICY_HPP_INCLUDED_

#include <boost/parameter/keyword.hpp>
#include <boost/log/detail/config.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace keywords {

//! The keyword allows to pass the policy for dealing with a full message queue
BOOST_PARAMETER_KEYWORD(tag, queue_policy)

} // namespace keywords

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#endif // BOOST_LOG_KEYWORDS_QUEUE_POLICY_HPP_INCLUDED_
