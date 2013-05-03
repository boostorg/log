/*
 *          Copyright Andrey Semashev 2007 - 2013.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   dump.cpp
 * \author Andrey Semashev
 * \date   03.05.2013
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/libs/log/doc/log.html.
 */

#include <ostream>
#include <boost/cstdint.hpp>
#include <boost/log/utility/manipulators/dump.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

enum { stride = 64 };

static const char g_lowercase_char_table[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
static const char g_uppercase_char_table[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

template< typename CharT >
void dump_data(const void* data, std::size_t size, std::basic_ostream< CharT >& strm)
{
    typedef CharT char_type;

    char_type buf[stride * 3u];

    const char* const char_table = (strm.flags() & std::ios_base::uppercase) ? g_uppercase_char_table : g_lowercase_char_table;
    const std::size_t stride_count = size / stride, tail_size = size % stride;

    const uint8_t* p = static_cast< const uint8_t* >(data);
    char_type* buf_begin = buf + 1u; // skip the first space of the first chunk
    char_type* buf_end = buf + sizeof(buf) / sizeof(*buf);

    for (std::size_t i = 0; i < stride_count; ++i)
    {
        char_type* b = buf;
        for (unsigned int j = 0; j < stride; ++j, b += 3u, ++p)
        {
            uint32_t n = *p;
            b[0] = static_cast< char_type >(' ');
            b[1] = static_cast< char_type >(char_table[n >> 4]);
            b[2] = static_cast< char_type >(char_table[n & 0x0F]);
        }

        strm.write(buf_begin, buf_end - buf_begin);
        buf_begin = buf;
    }

    if (tail_size > 0)
    {
        char_type* b = buf;
        unsigned int i = 0;
        do
        {
            uint32_t n = *p;
            b[0] = static_cast< char_type >(' ');
            b[1] = static_cast< char_type >(char_table[n >> 4]);
            b[2] = static_cast< char_type >(char_table[n & 0x0F]);
            ++i;
            ++p;
            b += 3u;
        }
        while (i < tail_size);

        strm.write(buf_begin, b - buf_begin);
    }
}

template BOOST_LOG_API
void dump_data< char >(const void* data, std::size_t size, std::basic_ostream< char >& strm);
template BOOST_LOG_API
void dump_data< wchar_t >(const void* data, std::size_t size, std::basic_ostream< wchar_t >& strm);
#if !defined(BOOST_NO_CXX11_CHAR16_T)
template BOOST_LOG_API
void dump_data< char16_t >(const void* data, std::size_t size, std::basic_ostream< char16_t >& strm);
#endif
#if !defined(BOOST_NO_CXX11_CHAR32_T)
template BOOST_LOG_API
void dump_data< char32_t >(const void* data, std::size_t size, std::basic_ostream< char32_t >& strm);
#endif

} // namespace aux

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

