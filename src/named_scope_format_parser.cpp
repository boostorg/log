/*
 *          Copyright Andrey Semashev 2007 - 2014.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   named_scope_format_parser.cpp
 * \author Andrey Semashev
 * \date   14.11.2012
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include <algorithm>
#include <boost/move/core.hpp>
#include <boost/move/utility.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <boost/spirit/include/karma_uint.hpp>
#include <boost/spirit/include/karma_generate.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/expressions/formatters/named_scope.hpp>
#include <boost/log/utility/formatting_ostream.hpp>
#include <boost/log/detail/header.hpp>

namespace karma = boost::spirit::karma;

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace expressions {

namespace aux {

BOOST_LOG_ANONYMOUS_NAMESPACE {

iterator_range< const char* > parse_function_name(string_literal const& signature, bool include_scope)
{
    // The algorithm basically is: find the opening parenthesis with function arguments and extract the function name that is directly leftmost of it.
    //
    // Unfortunately, C++ syntax is too complex and context-dependent, there may be parenthesis in the return type and also in template parameters
    // of the function class or the function itself. We cheat and ignore any template parameters at all by skipping any characters in top-level angle brackets.
    //
    // There is no such graceful solution to the function return types, parsing it correctly requires knowledge of types. Our trick here is to rely on the
    // assumption that function name immediately preceeds the opening parenthesis, while in case of function return types there is a space between the return type
    // of the function return type and the parenthesis. Technically, the space is not required by C++ grammar, so some compiler could omit it and the parser
    // would get confused by it. Also, some compiler could insert a space after the function name and that would break it too. But for now I can't find
    // another equivalently fast and viable solution.
    //
    // When the function name is detected, the algorithm searches for its beginning backwards, again skipping any template arguments. The beginning
    // is detected by encountering a delimiter character, which can be a space or another punctuation character that is not allowed in function names.
    // A colon served as a delimiter as well if the scope name is to be omitted.
    //
    // Operators pose another problem at this stage. They are curently not supported.
    //
    // Note that the algorithm should be tolerant to user's custom scope names which may not be function names at all. For this reason in case of any failure
    // just return the original string intact.

    const char* const begin = signature.c_str();
    const char* const end = begin + signature.size();
    const char* p = begin;
    while (p != end)
    {
        // Search for the opening parenthesis or template arguments
        p += std::strcspn(p, "(<");
        if (p == begin || p == end)
            break;

        char c = *p;
        if (c == '(')
        {
            c = *(p - 1);
            if (c != ' ')
            {
                // Assume we found the function name, find its beginning
                const char* const name_end = p--;
                while (p != begin)
                {
                    c = *p;
                    if (c == ' ' || c == '*' || c == '&' || (!include_scope && c == ':'))
                    {
                        const char* const name_begin = p + 1;
                        if (name_begin < name_end)
                        {
                            // We found it
                            return iterator_range< const char* >(name_begin, name_end);
                        }
                        else
                        {
                            // Function name cannot be empty
                            goto NotFoundL;
                        }
                    }
                    else if (c == '>')
                    {
                        // It's template parameters, skip them
                        unsigned int depth = 1;
                        --p;
                        while (p != begin && depth > 0)
                        {
                            c = *p;
                            if (c == '<')
                                --depth;
                            else if (c == '>')
                                ++depth;
                            --p;
                        }
                    }
                    else
                    {
                        --p;
                    }
                }

                // If it came to this then the supposed function name begins from the start of the signature string (i.e. no return type at all).
                // This is the case for constructors, destructors and conversion operators.
                return iterator_range< const char* >(p, name_end);
            }
            else
            {
                // This must be the function return type, process characters inside the parenthesis
                ++p;
            }
        }
        else if (c == '<')
        {
            // Template parameters opened
            unsigned int depth = 1;
            do
            {
                ++p;
                p += std::strcspn(p, "><");

                if (p == end)
                    break;
                c = *p;
                if (c == '>')
                    --depth;
                else
                    ++depth;
            }
            while (depth > 0);
        }
    }

NotFoundL:
    return iterator_range< const char* >(signature.c_str(), signature.c_str() + signature.size());
}

template< typename CharT >
class named_scope_formatter
{
    BOOST_COPYABLE_AND_MOVABLE_ALT(named_scope_formatter)

public:
    typedef void result_type;

    typedef CharT char_type;
    typedef std::basic_string< char_type > string_type;
    typedef basic_formatting_ostream< char_type > stream_type;
    typedef attributes::named_scope::value_type::value_type value_type;

    struct literal
    {
        typedef void result_type;

        explicit literal(string_type& lit) { m_literal.swap(lit); }

        result_type operator() (stream_type& strm, value_type const&) const
        {
            strm << m_literal;
        }

    private:
        string_type m_literal;
    };

    struct scope_name
    {
        typedef void result_type;

        result_type operator() (stream_type& strm, value_type const& value) const
        {
            strm << value.scope_name;
        }
    };

    struct function_name
    {
        typedef void result_type;

        explicit function_name(bool include_scope) : m_include_scope(include_scope)
        {
        }

        result_type operator() (stream_type& strm, value_type const& value) const
        {
            if (value.type == attributes::named_scope_entry::function)
            {
                iterator_range< const char* > function_name = parse_function_name(value.scope_name, m_include_scope);
                strm.write(function_name.begin(), function_name.size());
            }
            else
            {
                strm << value.scope_name;
            }
        }

    private:
        const bool m_include_scope;
    };

    struct full_file_name
    {
        typedef void result_type;

        result_type operator() (stream_type& strm, value_type const& value) const
        {
            strm << value.file_name;
        }
    };

    struct file_name
    {
        typedef void result_type;

        result_type operator() (stream_type& strm, value_type const& value) const
        {
            std::size_t n = value.file_name.size(), i = n;
            for (; i > 0; --i)
            {
                const char c = value.file_name[i - 1];
#if defined(BOOST_WINDOWS)
                if (c == '\\')
                    break;
#endif
                if (c == '/')
                    break;
            }
            strm.write(value.file_name.c_str() + i, n - i);
        }
    };

    struct line_number
    {
        typedef void result_type;

        result_type operator() (stream_type& strm, value_type const& value) const
        {
            strm.flush();
            typedef typename stream_type::streambuf_type streambuf_type;
            string_type& str = *static_cast< streambuf_type* >(strm.rdbuf())->storage();

            char_type buf[std::numeric_limits< unsigned int >::digits10 + 2];
            char_type* p = buf;

            typedef karma::uint_generator< unsigned int, 10 > uint_gen;
            karma::generate(p, uint_gen(), value.line);
            str.append(buf, p);
        }
    };

private:
    typedef boost::log::aux::light_function< void (stream_type&, value_type const&) > formatter_type;
    typedef std::vector< formatter_type > formatters;

private:
    formatters m_formatters;

public:
    BOOST_DEFAULTED_FUNCTION(named_scope_formatter(), {})
    named_scope_formatter(named_scope_formatter const& that) : m_formatters(that.m_formatters) {}
    named_scope_formatter(BOOST_RV_REF(named_scope_formatter) that) { m_formatters.swap(that.m_formatters); }

    named_scope_formatter& operator= (named_scope_formatter that)
    {
        this->swap(that);
        return *this;
    }

    result_type operator() (stream_type& strm, value_type const& value) const
    {
        for (typename formatters::const_iterator it = m_formatters.begin(), end = m_formatters.end(); strm.good() && it != end; ++it)
        {
            (*it)(strm, value);
        }
    }

#if !defined(BOOST_NO_CXX11_RVALUE_REFERENCES)
    template< typename FunT >
    void add_formatter(FunT&& fun)
    {
        m_formatters.emplace_back(boost::forward< FunT >(fun));
    }
#else
    template< typename FunT >
    void add_formatter(FunT const& fun)
    {
        m_formatters.push_back(formatter_type(fun));
    }
#endif

    void swap(named_scope_formatter& that)
    {
        m_formatters.swap(that.m_formatters);
    }
};

//! Parses the named scope format string and constructs the formatter function
template< typename CharT >
BOOST_FORCEINLINE boost::log::aux::light_function< void (basic_formatting_ostream< CharT >&, attributes::named_scope::value_type::value_type const&) >
do_parse_named_scope_format(const CharT* begin, const CharT* end)
{
    typedef CharT char_type;
    typedef boost::log::aux::light_function< void (basic_formatting_ostream< char_type >&, attributes::named_scope::value_type::value_type const&) > result_type;
    typedef named_scope_formatter< char_type > formatter_type;
    formatter_type fmt;

    std::basic_string< char_type > literal;

    while (begin != end)
    {
        const char_type* p = std::find(begin, end, static_cast< char_type >('%'));
        literal.append(begin, p);

        if ((end - p) >= 2)
        {
            switch (p[1])
            {
            case '%':
                literal.push_back(static_cast< char_type >('%'));
                break;

            case 'n':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::scope_name());
                break;

            case 'c':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::function_name(true));
                break;

            case 'C':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::function_name(false));
                break;

            case 'f':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::full_file_name());
                break;

            case 'F':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::file_name());
                break;

            case 'l':
                if (!literal.empty())
                    fmt.add_formatter(typename formatter_type::literal(literal));
                fmt.add_formatter(typename formatter_type::line_number());
                break;

            default:
                literal.append(p, p + 2);
                break;
            }

            begin = p + 2;
        }
        else
        {
            if (p != end)
                literal.push_back(static_cast< char_type >('%')); // a single '%' character at the end of the string
            begin = end;
        }
    }

    if (!literal.empty())
        fmt.add_formatter(typename formatter_type::literal(literal));

    return result_type(boost::move(fmt));
}

} // namespace


#ifdef BOOST_LOG_USE_CHAR

//! Parses the named scope format string and constructs the formatter function
BOOST_LOG_API boost::log::aux::light_function< void (basic_formatting_ostream< char >&, attributes::named_scope::value_type::value_type const&) >
parse_named_scope_format(const char* begin, const char* end)
{
    return do_parse_named_scope_format(begin, end);
}

#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T

//! Parses the named scope format string and constructs the formatter function
BOOST_LOG_API boost::log::aux::light_function< void (basic_formatting_ostream< wchar_t >&, attributes::named_scope::value_type::value_type const&) >
parse_named_scope_format(const wchar_t* begin, const wchar_t* end)
{
    return do_parse_named_scope_format(begin, end);
}

#endif // BOOST_LOG_USE_WCHAR_T

} // namespace aux

} // namespace expressions

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>
