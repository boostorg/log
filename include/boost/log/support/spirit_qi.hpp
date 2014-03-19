/*
 *          Copyright Andrey Semashev 2007 - 2014.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   support/spirit_qi.hpp
 * \author Andrey Semashev
 * \date   19.07.2009
 *
 * This header enables Boost.Spirit.Qi support for Boost.Log.
 */

#ifndef BOOST_LOG_SUPPORT_SPIRIT_QI_HPP_INCLUDED_
#define BOOST_LOG_SUPPORT_SPIRIT_QI_HPP_INCLUDED_

#include <boost/utility/enable_if.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/utility/functional/matches.hpp>
#include <boost/spirit/include/qi_parse.hpp>
#include <boost/spirit/home/qi/parser.hpp>
#include <boost/log/detail/header.hpp>

#ifdef BOOST_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

//! This tag type is used if an expression is recognized as a Boost.Spirit.Qi expression
struct boost_spirit_qi_expression_tag;

//! The metafunction detects the matching expression kind and returns a tag that is used to specialize \c match_traits
template< typename ExpressionT >
struct matching_expression_kind< ExpressionT, typename boost::enable_if< spirit::traits::is_parser< ExpressionT > >::type >
{
    typedef boost_spirit_qi_expression_tag type;
};

//! The matching function implementation
template< typename ExpressionT >
struct match_traits< ExpressionT, boost_spirit_qi_expression_tag >
{
    template< typename StringT >
    static bool matches(StringT const& str, ExpressionT const& expr)
    {
        typedef typename StringT::const_iterator const_iterator;
        const_iterator it = str.begin(), end = str.end();
        return (spirit::qi::parse(it, end, expr) && it == end);
    }
};

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_SUPPORT_SPIRIT_QI_HPP_INCLUDED_
