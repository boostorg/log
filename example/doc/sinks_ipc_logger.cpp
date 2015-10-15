/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#include <iostream>
#include <sstream>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/log/core.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;

//[ example_sinks_ipc_logger
int main()
{
    typedef sinks::text_ipc_message_queue_backend backend_t;
    typedef sinks::synchronous_sink<backend_t> sink_t;

    try
    {
        // Create a backend that is associated with the interprocess message queue
        // named "ipc_message_queue".
        boost::shared_ptr<backend_t> p_backend(new backend_t(
            keywords::message_queue_name = "ipc_message_queue",
            keywords::open_mode = backend_t::open_or_create,
            keywords::max_queue_size = 5,
            keywords::max_message_size = 30,
            keywords::queue_policy = backend_t::drop_when_full,
            keywords::message_policy = backend_t::truncate_when_too_long));
        boost::shared_ptr<sink_t> p_sink(new sink_t(p_backend));
        logging::core::get()->add_sink(p_sink);

        // Try to synthesize an identifier for the logger.
        src::logger logger;
        std::ostringstream stream;
        stream << boost::this_thread::get_id();
        std::string id = stream.str();
        id.resize(5, '0');
        std::cout << "Logger process " << id << " running..." << std::endl;

        // Keep sending numbered messages to the associated message queue until EOF.
        for (unsigned i = 1; std::cin.get() != std::istream::traits_type::eof(); ++i)
        {
            std::cout << "Send message #" << i << " from " << id << '.' << std::endl;
            BOOST_LOG(logger) << "Message #" << i << " from " << id << '.';
        }
    }
    catch (std::exception const& e)
    {
        std::cout << e.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "Unknown exception caught." << std::endl;
    }

    logging::core::get()->remove_all_sinks();
}
//]
