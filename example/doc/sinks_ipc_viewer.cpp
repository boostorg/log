/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#include <iostream>
#include <string>
#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>

namespace sinks = boost::log::sinks;

//[ example_sinks_ipc_viewer
int main()
{
    typedef sinks::text_ipc_message_queue_backend::message_queue_type queue_t;

    try
    {
        // Create a message_queue_type object that is associated with the interprocess
        // message queue named "ipc_message_queue".
        queue_t queue("ipc_message_queue", queue_t::open_or_create, 5, 30);
        
        std::cout << "Viewer process running..." << std::endl;
        
        char buffer[30] = {};
        // Keep reading log messages from the associated message queue until EOF.
        while (std::cin.get() != std::istream::traits_type::eof())
        {
            unsigned int message_size = 0;
            if (queue.try_receive(buffer, 30, message_size))
            {
                std::cout << std::string(buffer, buffer + message_size) << std::endl;
            }
            else
            {
                std::cout << "Message queue is empty. Nothing to receive." << std::endl;
            }
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
}
//]
