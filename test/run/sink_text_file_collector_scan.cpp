/*
 *            Copyright 2026, The MathWorks Inc.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   sink_text_file_collector_scan.cpp
 * \author Conor Burgess
 * \date   20.11.2026
 *
 * \brief  This file contains tests for the file collector of `text_file_backend` tracking every log file exactly once.
 */

#define BOOST_TEST_MODULE sink_text_file_collector_scan

#include <ctime>
#include <cstdio>
#include <ios>
#include <limits>
#include <string>
#include <boost/test/unit_test.hpp>
#include <boost/cstdint.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/smart_ptr/make_shared_object.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/keywords/file_name.hpp>
#include <boost/log/keywords/target.hpp>
#include <boost/log/keywords/max_files.hpp>
#include <boost/log/keywords/open_mode.hpp>

#include "make_record.hpp"

namespace fs = boost::filesystem;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;

namespace {

typedef boost::shared_ptr< sinks::text_file_backend > backend_ptr;

//! Creates a unique temporary directory and removes it with all its contents on destruction
struct temp_dir_fixture
{
    fs::path dir;

    temp_dir_fixture() :
        dir(fs::temp_directory_path() / fs::unique_path("boost_log_test_collector_scan_%%%%-%%%%-%%%%-%%%%"))
    {
        fs::create_directories(dir);
    }

    ~temp_dir_fixture()
    {
        boost::system::error_code ec;
        fs::remove_all(dir, ec);
    }

    void create_file(const char* name, std::time_t mtime = 0) const
    {
        const fs::path p = dir / name;
        {
            fs::ofstream strm(p);
            strm << "log record\n";
        }
        if (mtime != 0)
            fs::last_write_time(p, mtime);
    }

    bool file_exists(const char* name) const
    {
        return fs::exists(dir / name);
    }

    backend_ptr make_backend(const char* pattern, std::ios_base::openmode mode = std::ios_base::out | std::ios_base::trunc,
        boost::uintmax_t max_files = (std::numeric_limits< boost::uintmax_t >::max)()) const
    {
        backend_ptr backend = boost::make_shared< sinks::text_file_backend >(
            keywords::file_name = dir / pattern,
            keywords::open_mode = mode);
        backend->set_file_collector(sinks::file::make_collector(keywords::target = dir, keywords::max_files = max_files));
        return backend;
    }
};

//! Opens a log file in the backend by writing a record
void write_record(sinks::text_file_backend& backend)
{
    backend.consume(make_record_view(), "log record");
}

} // namespace

// Rescanning must not track the same files again
BOOST_FIXTURE_TEST_CASE(rescan_does_not_duplicate, temp_dir_fixture)
{
    for (unsigned int i = 0u; i < 5u; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "app_%05u.log", i);
        create_file(name);
    }
    create_file("unrelated.txt");

    backend_ptr backend = make_backend("app_%5N.log");
    BOOST_CHECK_EQUAL(backend->scan_for_files(), 5u);
    BOOST_CHECK_EQUAL(backend->scan_for_files(), 0u);
    BOOST_CHECK_EQUAL(backend->scan_for_files(sinks::file::scan_all), 1u); // only unrelated.txt is new
    BOOST_CHECK_EQUAL(backend->scan_for_files(sinks::file::scan_all), 0u);
}

// Sinks writing to the same directory share the file collector
BOOST_FIXTURE_TEST_CASE(shared_collector_scans, temp_dir_fixture)
{
    for (unsigned int i = 0u; i < 10u; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "primary_%05u.log", i);
        create_file(name);
    }
    for (unsigned int i = 0u; i < 3u; ++i)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "secondary_%05u.log", i);
        create_file(name);
    }
    create_file("unrelated.txt");

    backend_ptr primary = make_backend("primary_%5N.log");
    backend_ptr secondary = make_backend("secondary_%5N.log");

    BOOST_CHECK_EQUAL(primary->scan_for_files(), 10u);
    BOOST_CHECK_EQUAL(secondary->scan_for_files(), 3u);
    BOOST_CHECK_EQUAL(primary->scan_for_files(), 0u);
    BOOST_CHECK_EQUAL(secondary->scan_for_files(), 0u);
    BOOST_CHECK_EQUAL(primary->scan_for_files(sinks::file::scan_all), 1u); // only unrelated.txt is new
    BOOST_CHECK_EQUAL(secondary->scan_for_files(sinks::file::scan_all), 0u);
}

// The file counter must continue after the largest counter found by the scan
BOOST_FIXTURE_TEST_CASE(scan_updates_file_counter, temp_dir_fixture)
{
    create_file("app_00003.log");
    create_file("app_00007.log");
    create_file("other_00042.log");

    backend_ptr backend = make_backend("app_%5N.log");
    BOOST_CHECK_EQUAL(backend->scan_for_files(), 2u);

    write_record(*backend);
    BOOST_CHECK_EQUAL(backend->get_current_file_name().filename().string(), std::string("app_00008.log"));
}

// Tracked files must be recognized when the directory is spelled differently
BOOST_FIXTURE_TEST_CASE(scan_differently_spelled_directory, temp_dir_fixture)
{
    create_file("app_00000.log");
    create_file("app_00001.log");

    backend_ptr backend = make_backend("app_%5N.log");
    BOOST_CHECK_EQUAL(backend->scan_for_files(), 2u);

    // Returns the collector used by the backend
    boost::shared_ptr< sinks::file::collector > collector = sinks::file::make_collector(keywords::target = dir);
    const fs::path alt_pattern = dir / "." / "app_%5N.log";
    BOOST_CHECK_EQUAL(collector->scan_for_files(sinks::file::scan_matching, alt_pattern).found_count, 0u);

    create_file("app_00002.log");
    sinks::file::scan_result result = collector->scan_for_files(sinks::file::scan_matching, alt_pattern);
    BOOST_CHECK_EQUAL(result.found_count, 1u);
    BOOST_CHECK(!!result.last_file_counter);
    if (!!result.last_file_counter)
        BOOST_CHECK_EQUAL(*result.last_file_counter, 2u);

    BOOST_CHECK_EQUAL(backend->scan_for_files(), 0u);
}

// Rotating a file found by a scan must not track it twice, which would count it twice towards max_files
BOOST_FIXTURE_TEST_CASE(rotation_of_scanned_file_does_not_duplicate, temp_dir_fixture)
{
    const std::time_t now = std::time(NULL);
    create_file("app_00000.log", now - 300);
    create_file("app_00001.log", now - 200);
    create_file("app_00002.log", now - 100);

    backend_ptr backend = make_backend("app_%5N.log", std::ios_base::out | std::ios_base::app, 4u);
    BOOST_CHECK_EQUAL(backend->scan_for_files(), 3u);

    // Appends to the last scanned file
    write_record(*backend);
    BOOST_CHECK_EQUAL(backend->get_current_file_name().filename().string(), std::string("app_00002.log"));
    backend->rotate_file();

    write_record(*backend);
    BOOST_CHECK_EQUAL(backend->get_current_file_name().filename().string(), std::string("app_00003.log"));
    backend->rotate_file();

    // 4 files are within the limit
    BOOST_CHECK(file_exists("app_00000.log"));
    BOOST_CHECK(file_exists("app_00001.log"));
    BOOST_CHECK(file_exists("app_00002.log"));
    BOOST_CHECK(file_exists("app_00003.log"));

    // The fifth file evicts the oldest one
    write_record(*backend);
    backend->rotate_file();
    BOOST_CHECK(!file_exists("app_00000.log"));
    BOOST_CHECK(file_exists("app_00001.log"));
    BOOST_CHECK(file_exists("app_00004.log"));
}
