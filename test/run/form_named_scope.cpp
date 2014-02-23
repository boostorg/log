/*
 *          Copyright Andrey Semashev 2007 - 2014.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   form_named_scope.cpp
 * \author Andrey Semashev
 * \date   07.02.2009
 *
 * \brief  This header contains tests for the \c named_scope formatter.
 */

#define BOOST_TEST_MODULE form_named_scope

#include <string>
#include <boost/config.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/log/attributes/constant.hpp>
#include <boost/log/attributes/attribute_set.hpp>
#include <boost/log/attributes/named_scope.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/formatting_ostream.hpp>
#include <boost/log/utility/string_literal.hpp>
#include <boost/log/core/record.hpp>
#include "char_definitions.hpp"
#include "make_record.hpp"

namespace logging = boost::log;
namespace attrs = logging::attributes;
namespace expr = logging::expressions;
namespace keywords = logging::keywords;

namespace {

    template< typename CharT >
    struct named_scope_test_data;

    struct named_scope_test_data_base
    {
        static logging::string_literal scope1() { return logging::str_literal("scope1"); }
        static logging::string_literal scope2() { return logging::str_literal("scope2"); }

        static logging::string_literal function_name1() { return logging::str_literal("int main(int, char *[])"); }
        static logging::string_literal function_name2() { return logging::str_literal("int __cdecl main(int, char *[])"); }
        static logging::string_literal function_name3() { return logging::str_literal("namespace_name::type foo()"); }
        static logging::string_literal function_name4() { return logging::str_literal("namespace_name::type& foo::bar(int[], std::string const&)"); }
        static logging::string_literal function_name5() { return logging::str_literal("void* namespc::foo<char>::bar()"); }
        static logging::string_literal function_name6() { return logging::str_literal("void* namespc::foo<char>::bar<int>(int) const"); }
        static logging::string_literal function_name7() { return logging::str_literal("void (*)() namespc::foo<char (__stdcall*)()>::bar(int (my_class::*)(float*), my_class*) const volatile"); }
        static logging::string_literal function_name8() { return logging::str_literal("void (*)(const int (&)[]) namespc::foo<char (__stdcall*)()>::bar<char (__stdcall xxx::*)()>(int (my_class::*)(float*), my_class*)"); }

        static logging::string_literal file() { return logging::str_literal(__FILE__); }
        static logging::string_literal posix_file() { return logging::str_literal("/home/user/posix_file.cpp"); }
        static logging::string_literal windows_file1() { return logging::str_literal("C:\\user\\windows_file1.cpp"); }
        static logging::string_literal windows_file2() { return logging::str_literal("C:/user/windows_file2.cpp"); }
    };

#ifdef BOOST_LOG_USE_CHAR
    template< >
    struct named_scope_test_data< char > :
        public test_data< char >,
        public named_scope_test_data_base
    {
        static logging::string_literal default_format() { return logging::str_literal("%n"); }
        static logging::string_literal full_format() { return logging::str_literal("%n (%f:%l)"); }
        static logging::string_literal short_filename_format() { return logging::str_literal("%n (%F:%l)"); }
        static logging::string_literal scope_function_name_format() { return logging::str_literal("%c"); }
        static logging::string_literal function_name_format() { return logging::str_literal("%C"); }
        static logging::string_literal delimiter1() { return logging::str_literal("|"); }
    };
#endif // BOOST_LOG_USE_CHAR

#ifdef BOOST_LOG_USE_WCHAR_T
    template< >
    struct named_scope_test_data< wchar_t > :
        public test_data< wchar_t >,
        public named_scope_test_data_base
    {
        static logging::wstring_literal default_format() { return logging::str_literal(L"%n"); }
        static logging::wstring_literal full_format() { return logging::str_literal(L"%n (%f:%l)"); }
        static logging::wstring_literal short_filename_format() { return logging::str_literal(L"%n (%F:%l)"); }
        static logging::wstring_literal scope_function_name_format() { return logging::str_literal(L"%c"); }
        static logging::wstring_literal function_name_format() { return logging::str_literal(L"%C"); }
        static logging::wstring_literal delimiter1() { return logging::str_literal(L"|"); }
    };
#endif // BOOST_LOG_USE_WCHAR_T

    template< typename CharT >
    inline bool check_formatting(logging::basic_string_literal< CharT > const& format, logging::record_view const& rec, std::basic_string< CharT > const& expected)
    {
        typedef logging::basic_formatter< CharT > formatter;
        typedef std::basic_string< CharT > string;
        typedef logging::basic_formatting_ostream< CharT > osstream;
        typedef named_scope_test_data< CharT > data;

        string str;
        osstream strm(str);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(), keywords::format = format.c_str());
        f(rec, strm);
        return equal_strings(strm.str(), expected);
    }

} // namespace

// The test checks that named scopes stack formatting works
BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_formatting, CharT, char_types)
{
    typedef attrs::named_scope named_scope;
    typedef named_scope::sentry sentry;
    typedef attrs::named_scope_list scopes;
    typedef attrs::named_scope_entry scope;

    typedef logging::attribute_set attr_set;
    typedef std::basic_string< CharT > string;
    typedef logging::basic_formatting_ostream< CharT > osstream;
    typedef logging::basic_formatter< CharT > formatter;
    typedef logging::record_view record_view;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;
    sentry scope1(data::scope1(), data::file(), line1);
    const unsigned int line2 = __LINE__;
    sentry scope2(data::scope2(), data::file(), line2);

    attr_set set1;
    set1[data::attr1()] = attr;

    record_view rec = make_record_view(set1);

    // Default format
    {
        string str;
        osstream strm(str);
        strm << data::scope1() << "->" << data::scope2();
        BOOST_CHECK(check_formatting(data::default_format(), rec, strm.str()));
    }
    // Full format
    {
        string str;
        osstream strm(str);
        strm << data::scope1() << " (" << data::file() << ":" << line1 << ")->"
             << data::scope2() << " (" << data::file() << ":" << line2 << ")";
        BOOST_CHECK(check_formatting(data::full_format(), rec, strm.str()));
    }
    // Different delimiter
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::delimiter = data::delimiter1().c_str());
        f(rec, strm1);
        strm2 << data::scope1() << "|" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Different direction
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::iteration = expr::reverse);
        f(rec, strm1);
        strm2 << data::scope2() << "<-" << data::scope1();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::iteration = expr::reverse);
        f(rec, strm1);
        strm2 << data::scope2() << "|" << data::scope1();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    // Limiting the number of scopes
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::depth = 1);
        f(rec, strm1);
        strm2 << "...->" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::depth = 1,
            keywords::iteration = expr::reverse);
        f(rec, strm1);
        strm2 << data::scope2() << "<-...";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::depth = 1);
        f(rec, strm1);
        strm2 << "...|" << data::scope2();
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
    {
        string str1, str2;
        osstream strm1(str1), strm2(str2);
        formatter f = expr::stream << expr::format_named_scope(data::attr1(),
            keywords::format = data::default_format().c_str(),
            keywords::delimiter = data::delimiter1().c_str(),
            keywords::depth = 1,
            keywords::iteration = expr::reverse);
        f(rec, strm1);
        strm2 << data::scope2() << "|...";
        BOOST_CHECK(equal_strings(strm1.str(), strm2.str()));
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_filename_formatting_posix, CharT, char_types)
{
    typedef attrs::named_scope named_scope;
    typedef named_scope::sentry sentry;
    typedef attrs::named_scope_list scopes;
    typedef attrs::named_scope_entry scope;

    typedef logging::attribute_set attr_set;
    typedef std::basic_string< CharT > string;
    typedef logging::basic_formatting_ostream< CharT > osstream;
    typedef logging::record_view record_view;
    typedef logging::basic_formatter< CharT > formatter;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;
    sentry scope1(data::scope1(), data::posix_file(), line1);

    attr_set set1;
    set1[data::attr1()] = attr;

    record_view rec = make_record_view(set1);

    // File names without the full path
    {
        string str;
        osstream strm(str);
        strm << data::scope1() << " (posix_file.cpp:" << line1 << ")";
        BOOST_CHECK(check_formatting(data::short_filename_format(), rec, strm.str()));
    }
}

#if defined(BOOST_WINDOWS)

BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_filename_formatting_windows, CharT, char_types)
{
    typedef attrs::named_scope named_scope;
    typedef named_scope::sentry sentry;
    typedef attrs::named_scope_list scopes;
    typedef attrs::named_scope_entry scope;

    typedef logging::attribute_set attr_set;
    typedef std::basic_string< CharT > string;
    typedef logging::basic_formatting_ostream< CharT > osstream;
    typedef logging::record_view record_view;
    typedef logging::basic_formatter< CharT > formatter;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;
    sentry scope1(data::scope1(), data::windows_file1(), line1);
    const unsigned int line2 = __LINE__;
    sentry scope2(data::scope2(), data::windows_file2(), line2);

    attr_set set1;
    set1[data::attr1()] = attr;

    record_view rec = make_record_view(set1);

    // File names without the full path
    {
        string str;
        osstream strm(str);
        strm << data::scope1() << " (windows_file1.cpp:" << line1 << ")->"
             << data::scope2() << " (windows_file2.cpp:" << line2 << ")";
        BOOST_CHECK(check_formatting(data::short_filename_format(), rec, strm.str()));
    }
}

#endif // defined(BOOST_WINDOWS)

// Function name formatting
BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_scope_function_name_formatting, CharT, char_types)
{
    typedef attrs::named_scope named_scope;
    typedef named_scope::sentry sentry;
    typedef attrs::named_scope_list scopes;
    typedef attrs::named_scope_entry scope;

    typedef logging::attribute_set attr_set;
    typedef std::basic_string< CharT > string;
    typedef logging::basic_formatting_ostream< CharT > osstream;
    typedef logging::record_view record_view;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;

    attr_set set1;
    set1[data::attr1()] = attr;

    record_view rec = make_record_view(set1);

    // File names without the full path
    {
        sentry scope1(data::function_name1(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "main";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name2(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "main";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name3(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "foo";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name4(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "foo::bar";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name5(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "namespc::foo<char>::bar";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name6(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "namespc::foo<char>::bar<int>";
        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
    }
//    {
//        sentry scope1(data::function_name7(), data::file(), line1);
//        string str;
//        osstream strm(str);
//        strm << "namespc::foo<char (__stdcall*)()>::bar";
//        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
//    }
//    {
//        sentry scope1(data::function_name8(), data::file(), line1);
//        string str;
//        osstream strm(str);
//        strm << "namespc::foo<char (__stdcall*)()>::bar<char (__stdcall xxx::*)()>";
//        BOOST_CHECK(check_formatting(data::scope_function_name_format(), rec, strm.str()));
//    }
}

// Function name without scope formatting
BOOST_AUTO_TEST_CASE_TEMPLATE(scopes_function_name_formatting, CharT, char_types)
{
    typedef attrs::named_scope named_scope;
    typedef named_scope::sentry sentry;
    typedef attrs::named_scope_list scopes;
    typedef attrs::named_scope_entry scope;

    typedef logging::attribute_set attr_set;
    typedef std::basic_string< CharT > string;
    typedef logging::basic_formatting_ostream< CharT > osstream;
    typedef logging::record_view record_view;
    typedef named_scope_test_data< CharT > data;

    named_scope attr;

    // First scope
    const unsigned int line1 = __LINE__;

    attr_set set1;
    set1[data::attr1()] = attr;

    record_view rec = make_record_view(set1);

    // File names without the full path
    {
        sentry scope1(data::function_name1(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "main";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name2(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "main";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name3(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "foo";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name4(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "bar";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name5(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "bar";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
    {
        sentry scope1(data::function_name6(), data::file(), line1);
        string str;
        osstream strm(str);
        strm << "bar<int>";
        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
    }
//    {
//        sentry scope1(data::function_name7(), data::file(), line1);
//        string str;
//        osstream strm(str);
//        strm << "bar";
//        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
//    }
//    {
//        sentry scope1(data::function_name8(), data::file(), line1);
//        string str;
//        osstream strm(str);
//        strm << "bar<char (__stdcall xxx::*)()>";
//        BOOST_CHECK(check_formatting(data::function_name_format(), rec, strm.str()));
//    }
}

