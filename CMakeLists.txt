# Copyright 2018 Peter Dimov
# Copyright 2018 Andrey Semashev
# Distributed under the Boost Software License, Version 1.0.
# See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt

# Partial (add_subdirectory only) and experimental CMake support
# Subject to change; please do not rely on the contents of this file yet.
# This file is NOT supposed to be used to build the library, use Boost.Build for that.

cmake_minimum_required(VERSION 3.5)

project(BoostLog LANGUAGES CXX)

add_library(boost_log INTERFACE)
add_library(Boost::log ALIAS boost_log)

target_include_directories(boost_log INTERFACE include)

target_link_libraries(boost_log
    INTERFACE
        Boost::array
        Boost::assert
        Boost::atomic
        Boost::bind
        Boost::config
        Boost::container
        Boost::core
        Boost::date_time
        Boost::exception
        Boost::filesystem
        Boost::function_types
        Boost::fusion
        Boost::intrusive
        Boost::iterator
        Boost::lexical_cast
        Boost::locale
        Boost::move
        Boost::mpl
        Boost::optional
        Boost::parameter
        Boost::phoenix
        Boost::predef
        Boost::preprocessor
        Boost::property_tree
        Boost::proto
        Boost::range
        Boost::regex
        Boost::smart_ptr
        Boost::spirit
        Boost::static_assert
        Boost::system
        Boost::thread
        Boost::throw_exception
        Boost::type_index
        Boost::type_traits
        Boost::utility
        Boost::winapi
        Boost::xpressive
)
