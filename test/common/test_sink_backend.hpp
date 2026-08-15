//  Copyright (c) 2026 Andrey Semashev
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_LOG_TEST_SINK_BACKEND_HPP_INCLUDED_
#define BOOST_LOG_TEST_SINK_BACKEND_HPP_INCLUDED_

#include <boost/log/core/record_view.hpp>
#include <boost/log/sinks/basic_sink_backend.hpp>
#include <boost/log/sinks/frontend_requirements.hpp>

/*!
 * \brief An implementation of a test logging sink backend
 *
 * The sink backend does not do anything with the log records it receives.
 */
class test_backend :
    public boost::log::sinks::basic_sink_backend<
        boost::log::sinks::combine_requirements<
            boost::log::sinks::synchronized_feeding,
            boost::log::sinks::flushing
        >::type
    >
{
    //! Base type
    using base_type = boost::log::sinks::basic_sink_backend<
        boost::log::sinks::combine_requirements<
            boost::log::sinks::synchronized_feeding,
            boost::log::sinks::flushing
        >::type
    >;

public:
    //! The method receives the log message
    void consume(boost::log::record_view const& rec) {}

    //! The method implements flushing any buffers
    void flush() {}
};

#endif // BOOST_LOG_TEST_SINK_BACKEND_HPP_INCLUDED_
