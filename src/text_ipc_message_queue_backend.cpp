/*
 *                 Copyright Lingxi Li 2015.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   text_ipc_message_queue_backend.cpp
 * \author Lingxi Li
 * \date   18.10.2015
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <boost/smart_ptr/scoped_ptr.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/log/sinks/text_ipc_message_queue_backend.hpp>
#include "ipc_message_queue_posix.hpp"
#include "ipc_message_queue_win.hpp"
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace sinks {

////////////////////////////////////////////////////////////////////////////////
//  Interprocess message queue sink backend implementation
////////////////////////////////////////////////////////////////////////////////
//! Sink implementation data
template < typename CharT >
struct basic_text_ipc_message_queue_backend< CharT >::implementation
{
    message_queue_type  m_MessageQueue;
    queue_policy_type   m_QueuePolicy;
    message_policy_type m_MessagePolicy;
};

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::open_mode const 
    basic_text_ipc_message_queue_backend< CharT >::create_only;

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::open_mode const
    basic_text_ipc_message_queue_backend< CharT >::open_only;

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::open_mode const
    basic_text_ipc_message_queue_backend< CharT >::open_or_create;

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::basic_text_ipc_message_queue_backend()
{
    construct(log::aux::empty_arg_list());
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::construct(
  char const* message_queue_name,
  open_mode mode,
  unsigned int max_queue_size,
  unsigned int max_message_size,
  queue_policy_type queue_policy_val,
  message_policy_type message_policy_val,
  permission const& permission_value)
{
    m_pImpl = new implementation();
    m_pImpl->m_MessageQueue.open(message_queue_name, mode, max_queue_size, 
        max_message_size, permission_value);
    m_pImpl->m_QueuePolicy = queue_policy_val;
    m_pImpl->m_MessagePolicy = message_policy_val;
}

template < typename CharT >
BOOST_LOG_API basic_text_ipc_message_queue_backend< CharT >::~basic_text_ipc_message_queue_backend()
{
    delete m_pImpl;
}

template < typename CharT>
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_queue_type&
basic_text_ipc_message_queue_backend< CharT >::message_queue()
{
    return m_pImpl->m_MessageQueue;
}

template < typename CharT>
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_queue_type const&
basic_text_ipc_message_queue_backend< CharT >::message_queue() const
{
    return m_pImpl->m_MessageQueue;
}

template < typename CharT >
BOOST_LOG_API std::string basic_text_ipc_message_queue_backend< CharT >::name() const
{
    return message_queue().name();
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::open(
  char const* name, open_mode mode, unsigned int max_queue_size, unsigned int max_message_size,
  permission const& permission_value)
{
    return message_queue().open(name, mode, max_queue_size, max_message_size, permission_value);
}

template < typename CharT >
BOOST_LOG_API bool basic_text_ipc_message_queue_backend< CharT >::is_open() const
{
    return message_queue().is_open();
}

template < typename CharT >
BOOST_LOG_API unsigned int basic_text_ipc_message_queue_backend< CharT >::max_queue_size() const
{
    return message_queue().max_queue_size();
}

template < typename CharT >
BOOST_LOG_API unsigned int basic_text_ipc_message_queue_backend< CharT >::max_message_size() const
{
    return message_queue().max_message_size();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::stop()
{
    message_queue().stop();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::reset()
{
    message_queue().reset();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::close()
{
    message_queue().close();
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::set_queue_policy(queue_policy_type policy)
{
    m_pImpl->m_QueuePolicy = policy;
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::set_message_policy(message_policy_type policy)
{
    m_pImpl->m_MessagePolicy = policy;
}

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::queue_policy_type
basic_text_ipc_message_queue_backend< CharT >::queue_policy() const
{
    return m_pImpl->m_QueuePolicy;
}

template < typename CharT >
BOOST_LOG_API typename basic_text_ipc_message_queue_backend< CharT >::message_policy_type
basic_text_ipc_message_queue_backend< CharT >::message_policy() const
{
    return m_pImpl->m_MessagePolicy;
}

template < typename CharT >
BOOST_LOG_API void basic_text_ipc_message_queue_backend< CharT >::consume(
  record_view const&, string_type const& message)
{
    // No associated message queue, return directly.
    if (!is_open()) return;

    unsigned int message_size = static_cast< unsigned int >(message.size() * sizeof(char_type));
    
    // Deal with messages that are too long.
    if (message_size > max_message_size())
    {
        switch (message_policy())
        {
        case drop_when_too_long:
            return;
        case truncate_when_too_long:
            message_size = max_message_size();
            break;
        default: // case throw_when_too_long:
            throw std::logic_error("Message is too long.");
        }
    }

    // Send message.
    if (queue_policy() == block_when_full)
    {
        message_queue().send(message.c_str(), message_size);
    }
    else
    {
        bool succeeded = message_queue().try_send(message.c_str(), message_size);
        if (queue_policy() == throw_when_full && !succeeded)
        {
            throw std::runtime_error("Message queue is full.");
        }
    }
}

//! Explicitly instantiate sink backend implementation
#ifdef BOOST_LOG_USE_CHAR
template class basic_text_ipc_message_queue_backend< char >;
#endif
#ifdef BOOST_LOG_USE_WCHAR_T
template class basic_text_ipc_message_queue_backend< wchar_t >;
#endif

} // namespace sinks

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>
