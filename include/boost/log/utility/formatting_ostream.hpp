/*
 *          Copyright Andrey Semashev 2007 - 2013.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   formatting_ostream.hpp
 * \author Andrey Semashev
 * \date   11.07.2012
 *
 * The header contains implementation of a string stream used for log record formatting.
 */

#ifndef BOOST_LOG_UTILITY_FORMATTING_OSTREAM_HPP_INCLUDED_
#define BOOST_LOG_UTILITY_FORMATTING_OSTREAM_HPP_INCLUDED_

#include <ostream>
#include <string>
#include <memory>
#include <locale>
#include <boost/ref.hpp>
#include <boost/type_traits/remove_cv.hpp>
#include <boost/utility/addressof.hpp>
#include <boost/utility/base_from_member.hpp>
#include <boost/log/detail/config.hpp>
#include <boost/log/detail/attachable_sstream_buf.hpp>
#include <boost/log/detail/code_conversion.hpp>
#include <boost/log/detail/parameter_tools.hpp>
#include <boost/log/utility/string_literal_fwd.hpp>
#include <boost/log/utility/formatting_ostream_fwd.hpp>
#include <boost/log/detail/header.hpp>

#ifdef BOOST_LOG_HAS_PRAGMA_ONCE
#pragma once
#endif

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace aux {

template< typename T, typename R, typename VoidT = void >
struct enable_basic_formatting_ostream_generic_insert_operator { typedef R type; };
template< typename T, typename R >
struct enable_basic_formatting_ostream_generic_insert_operator< T, R, typename T::_has_basic_formatting_ostream_insert_operator > {};
template< typename T, typename R >
struct enable_basic_formatting_ostream_generic_insert_operator< T*, R > {};
template< typename CharT, typename TraitsT, typename AllocatorT, typename R >
struct enable_basic_formatting_ostream_generic_insert_operator< std::basic_string< CharT, TraitsT, AllocatorT >, R > {};
template< typename CharT, typename TraitsT, typename R >
struct enable_basic_formatting_ostream_generic_insert_operator< basic_string_literal< CharT, TraitsT >, R > {};

template< typename T, typename R >
struct enable_if_char_type {};
template< typename R >
struct enable_if_char_type< char, R > { typedef R type; };
template< typename R >
struct enable_if_char_type< wchar_t, R > { typedef R type; };
#if !defined(BOOST_NO_CXX11_CHAR16_T)
template< typename R >
struct enable_if_char_type< char16_t, R > { typedef R type; };
#endif
#if !defined(BOOST_NO_CXX11_CHAR32_T)
template< typename R >
struct enable_if_char_type< char32_t, R > { typedef R type; };
#endif

} // namespace aux

/*!
 * \brief Stream for log records formatting.
 *
 * This stream type is used by the library for log record formatting. It implements the standard string stream interface
 * with a few extensions:
 *
 * \li By default, \c bool values are formatted using alphabetical representation rather than numeric.
 * \li The stream supports writing strings of character types different from the stream character type. The stream will perform
 *     character code conversion as needed using the imbued locale.
 * \li The stream operates on an external string object rather than on the embedded one. The string can be attached or detached
 *     from the stream dynamically.
 */
template< typename CharT, typename TraitsT, typename AllocatorT >
class basic_formatting_ostream :
    private base_from_member< boost::log::aux::basic_ostringstreambuf< CharT, TraitsT, AllocatorT > >,
    public std::basic_ostream< CharT, TraitsT >
{
    typedef base_from_member< boost::log::aux::basic_ostringstreambuf< CharT, TraitsT, AllocatorT > > streambuf_base_type;

public:
    //! Character type
    typedef CharT char_type;
    //! Character traits
    typedef TraitsT traits_type;
    //! Memory allocator
    typedef AllocatorT allocator_type;
    //! Stream buffer type
    typedef boost::log::aux::basic_ostringstreambuf< char_type, traits_type, allocator_type > streambuf_type;
    //! Target string type
    typedef typename streambuf_type::string_type string_type;

    //! Stream type
    typedef std::basic_ostream< char_type, traits_type > ostream_type;
    //! Stream position type
    typedef typename ostream_type::pos_type pos_type;
    //! Stream offset type
    typedef typename ostream_type::off_type off_type;

private:
    //  Function types
    typedef std::ios_base& (*ios_base_manip)(std::ios_base&);
    typedef std::basic_ios< char_type, traits_type >& (*basic_ios_manip)(std::basic_ios< char_type, traits_type >&);
    typedef ostream_type& (*stream_manip)(ostream_type&);

public:
    /*!
     * Default constructor. Creates an empty record that is equivalent to the invalid record handle.
     * The stream capability is not available after construction.
     *
     * \post <tt>!*this == true</tt>
     */
    basic_formatting_ostream() : ostream_type(&this->streambuf_base_type::member)
    {
        init_stream();
    }

    /*!
     * Initializing constructor. Attaches the string to the constructed stream.
     * The string will be used to store the formatted characters.
     *
     * \post <tt>!*this == false</tt>
     * \param str The string buffer to attach.
     */
    explicit basic_formatting_ostream(string_type& str) :
        streambuf_base_type(boost::ref(str)),
        ostream_type(&this->streambuf_base_type::member)
    {
        init_stream();
    }

    /*!
     * Destructor. Destroys the record, releases any sinks and attribute values that were involved in processing this record.
     */
    ~basic_formatting_ostream()
    {
        if (this->streambuf_base_type::member.storage())
            flush();
    }

    /*!
     * Attaches the stream to the string. The string will be used to store the formatted characters.
     *
     * \param str The string buffer to attach.
     */
    void attach(string_type& str)
    {
        streambuf_base_type::member.attach(str);
        ostream_type::clear(ostream_type::goodbit);
    }
    /*!
     * Detaches the stream from the string. Any buffered data is flushed to the string.
     */
    void detach()
    {
        streambuf_base_type::member.detach();
        ostream_type::clear(ostream_type::badbit);
    }

    /*!
     * \returns Reference to the attached string. The string must be attached before calling this method.
     */
    string_type const& str()
    {
        flush();

        string_type* storage = this->streambuf_base_type::member.storage();
        BOOST_ASSERT(storage != NULL);

        return *storage;
    }

    //  Stream method overrides
    basic_formatting_ostream& flush()
    {
        ostream_type::flush();
        return *this;
    }

    basic_formatting_ostream& seekp(pos_type pos)
    {
        ostream_type::seekp(pos);
        return *this;
    }

    basic_formatting_ostream& seekp(off_type off, std::ios_base::seekdir dir)
    {
        ostream_type::seekp(off, dir);
        return *this;
    }

    streambuf_type* rdbuf()
    {
        return &this->streambuf_base_type::member;
    }

    basic_formatting_ostream& put(char_type c)
    {
        ostream_type::put(c);
        return *this;
    }

    template< typename OtherCharT >
    typename aux::enable_if_char_type< OtherCharT, basic_formatting_ostream& >::type
    put(OtherCharT c)
    {
        write(boost::addressof(c), 1);
        return *this;
    }

    basic_formatting_ostream& write(const char_type* p, std::streamsize size)
    {
        ostream_type::write(p, size);
        return *this;
    }

    template< typename OtherCharT >
    typename aux::enable_if_char_type< OtherCharT, basic_formatting_ostream& >::type
    write(const OtherCharT* p, std::streamsize size)
    {
        flush();

        string_type* storage = this->streambuf_base_type::member.storage();
        BOOST_ASSERT(storage != NULL);
        aux::code_convert(p, static_cast< std::size_t >(size), *storage, this->getloc());

        return *this;
    }

    basic_formatting_ostream& operator<< (ios_base_manip manip)
    {
        *static_cast< ostream_type* >(this) << manip;
        return *this;
    }
    basic_formatting_ostream& operator<< (basic_ios_manip manip)
    {
        *static_cast< ostream_type* >(this) << manip;
        return *this;
    }
    basic_formatting_ostream& operator<< (stream_manip manip)
    {
        *static_cast< ostream_type* >(this) << manip;
        return *this;
    }

    basic_formatting_ostream& operator<< (char c)
    {
        this->put(c);
        return *this;
    }
    basic_formatting_ostream& operator<< (const char* p)
    {
        this->write(p, static_cast< std::streamsize >(std::char_traits< char >::length(p)));
        return *this;
    }

#if !defined(BOOST_NO_INTRINSIC_WCHAR_T)
    basic_formatting_ostream& operator<< (wchar_t c)
    {
        this->put(c);
        return *this;
    }
    basic_formatting_ostream& operator<< (const wchar_t* p)
    {
        this->write(p, static_cast< std::streamsize >(std::char_traits< wchar_t >::length(p)));
        return *this;
    }
#endif
#if !defined(BOOST_NO_CXX11_CHAR16_T)
    basic_formatting_ostream& operator<< (char16_t c)
    {
        this->put(c);
        return *this;
    }
    basic_formatting_ostream& operator<< (const char16_t* p)
    {
        this->write(p, static_cast< std::streamsize >(std::char_traits< char16_t >::length(p)));
        return *this;
    }
#endif
#if !defined(BOOST_NO_CXX11_CHAR32_T)
    basic_formatting_ostream& operator<< (char32_t c)
    {
        this->put(c);
        return *this;
    }
    basic_formatting_ostream& operator<< (const char32_t* p)
    {
        this->write(p, static_cast< std::streamsize >(std::char_traits< char32_t >::length(p)));
        return *this;
    }
#endif

    basic_formatting_ostream& operator<< (bool value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (signed char value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (unsigned char value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (short value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (unsigned short value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (int value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (unsigned int value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (long value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (unsigned long value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
#if !defined(BOOST_NO_LONG_LONG)
    basic_formatting_ostream& operator<< (long long value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (unsigned long long value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
#endif

    basic_formatting_ostream& operator<< (float value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (double value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }
    basic_formatting_ostream& operator<< (long double value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }

    basic_formatting_ostream& operator<< (const void* value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }

    basic_formatting_ostream& operator<< (std::basic_streambuf< char_type, traits_type >* buf)
    {
        *static_cast< ostream_type* >(this) << buf;
        return *this;
    }

    template< typename OtherCharT, typename OtherTraitsT, typename OtherAllocatorT >
    typename aux::enable_if_char_type< OtherCharT, basic_formatting_ostream& >::type
    operator<< (std::basic_string< OtherCharT, OtherTraitsT, OtherAllocatorT > const& str)
    {
        this->write(str.c_str(), static_cast< std::streamsize >(str.size()));
        return *this;
    }

    template< typename OtherCharT, typename OtherTraitsT >
    typename aux::enable_if_char_type< OtherCharT, basic_formatting_ostream& >::type
    operator<< (basic_string_literal< OtherCharT, OtherTraitsT > const& str)
    {
        this->write(str.c_str(), static_cast< std::streamsize >(str.size()));
        return *this;
    }

    template< typename T >
    typename aux::enable_basic_formatting_ostream_generic_insert_operator< T, basic_formatting_ostream& >::type
    operator<< (T const& value)
    {
        *static_cast< ostream_type* >(this) << value;
        return *this;
    }

private:
    void init_stream()
    {
        ostream_type::clear(this->streambuf_base_type::member.storage() ? ostream_type::goodbit : ostream_type::badbit);
        ostream_type::flags
        (
            ostream_type::dec |
            ostream_type::skipws |
            ostream_type::boolalpha // this differs from the default stream flags but makes logs look better
        );
        ostream_type::width(0);
        ostream_type::precision(6);
        ostream_type::fill(static_cast< char_type >(' '));
    }

    //! Copy constructor (closed)
    BOOST_LOG_DELETED_FUNCTION(basic_formatting_ostream(basic_formatting_ostream const& that))
    //! Assignment (closed)
    BOOST_LOG_DELETED_FUNCTION(basic_formatting_ostream& operator= (basic_formatting_ostream const& that))
};

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_UTILITY_FORMATTING_OSTREAM_HPP_INCLUDED_
