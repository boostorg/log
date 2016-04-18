/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   keywords/max_message_size.hpp
 * \author Lingxi Li
 * \date   16.10.2015
 *
 * The header contains the \c max_message_size keyword declaration.
 */

#ifndef BOOST_LOG_KEYWORDS_MAX_MESSAGE_SIZE_HPP_INCLUDED_
#define BOOST_LOG_KEYWORDS_MAX_MESSAGE_SIZE_HPP_INCLUDED_

#include <boost/parameter/keyword.hpp>
#include <boost/log/detail/config.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace keywords {

//! The keyword allows to pass the maximum size of a message
BOOST_PARAMETER_KEYWORD(tag, max_message_size)

} // namespace keywords

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#endif // BOOST_LOG_KEYWORDS_MAX_MESSAGE_SIZE_HPP_INCLUDED_
