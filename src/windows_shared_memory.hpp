/*
 *              Copyright Andrey Semashev 2016.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */
/*!
 * \file   windows_shared_memory.hpp
 * \author Andrey Semashev
 * \date   13.02.2016
 *
 * \brief  This header is the Boost.Log library implementation, see the library documentation
 *         at http://www.boost.org/doc/libs/release/libs/log/doc/html/index.html.
 */

#ifndef BOOST_LOG_WINDOWS_SHARED_MEMORY_HPP_INCLUDED_
#define BOOST_LOG_WINDOWS_SHARED_MEMORY_HPP_INCLUDED_

#include <boost/log/detail/config.hpp>
#include <boost/detail/winapi/basic_types.hpp>
#include <boost/detail/winapi/handles.hpp>
#include <boost/detail/winapi/dll.hpp>
#include <boost/detail/winapi/file_mapping.hpp>
#include <boost/detail/winapi/page_protection_flags.hpp>
#include <boost/detail/winapi/get_last_error.hpp>
#include <windows.h> // for error codes
#include <cstddef>
#include <limits>
#include <string>
#include <utility>
#include <boost/assert.hpp>
#include <boost/memory_order.hpp>
#include <boost/atomic/atomic.hpp>
#include <boost/throw_exception.hpp>
#include <boost/log/exceptions.hpp>
#include <boost/log/utility/permissions.hpp>
#include <boost/log/detail/header.hpp>

namespace boost {

BOOST_LOG_OPEN_NAMESPACE

namespace ipc {

namespace aux {

/*!
 * A replacement for to \c windows_shared_memory and \c mapped_region from Boost.Interprocess.
 * The significant difference is that the shared memory name is passed as a UTF-16 string and
 * errors are reported as Boost.Log exceptions.
 */
class windows_shared_memory
{
private:
    typedef boost::detail::winapi::DWORD_ NTSTATUS_;
    struct section_basic_information
    {
        void* base_address;
        boost::detail::winapi::ULONG_ section_attributes;
        boost::detail::winapi::LARGE_INTEGER_ section_size;
    };
    typedef NTSTATUS_ (__stdcall *nt_query_section_t)(boost::detail::winapi::HANDLE_ h, unsigned int info_class, section_basic_information* pinfo, boost::detail::winapi::ULONG_ info_size, boost::detail::winapi::ULONG_* ret_len);

private:
    boost::detail::winapi::HANDLE_ m_handle;
    void* m_mapped_address;
    std::size_t m_size;
    static boost::atomic< nt_query_section_t > nt_query_section;
    //! A special permission that is required to be able to read the shared memory segment size
    static const boost::detail::winapi::DWORD_ SECTION_QUERY_ = 0x00000001;

public:
    BOOST_CONSTEXPR windows_shared_memory() BOOST_NOEXCEPT :
        m_handle(NULL)
        m_mapped_address(NULL),
        m_size(0u)
    {
    }

    ~windows_shared_memory()
    {
        if (m_mapped_address)
            unmap();

        if (m_handle)
        {
            BOOST_VERIFY(boost::detail::winapi::CloseHandle(m_handle) != 0);
            m_handle = NULL;
        }
    }

    //! Creates a new file mapping for the shared memory segment or opens the existing one
    void create_or_open(const wchar_t* name, std::size_t size, permissions const& perms = permissions())
    {
        BOOST_ASSERT(m_handle == NULL);

        // Unlike other create functions, this function opens the existing mapping, if one already exists
        boost::detail::winapi::HANDLE_ h = boost::detail::winapi::CreateFileMappingW
        (
            boost::detail::winapi::INVALID_HANDLE_VALUE_,
            reinterpret_cast< boost::detail::winapi::SECURITY_ATTRIBUTES_* >(perms.get_native()),
            boost::detail::winapi::PAGE_READWRITE_ | boost::detail::winapi::SEC_COMMIT_,
            static_cast< boost::detail::winapi::DWORD_ >(size >> 32u),
            static_cast< boost::detail::winapi::DWORD_ >(size),
            name
        );

        boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
        if (BOOST_UNLIKELY(h == NULL))
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to create a shared memory segment", (err));

        try
        {
            if (err == ERROR_ALREADY_EXISTS)
            {
                // If an existing segment was opened, determine its size
                m_size = obtain_size(h);
            }
            else
            {
                m_size = size;
            }
        }
        catch (...)
        {
            boost::detail::winapi::CloseHandle(h);
            throw;
        }

        m_handle = h;
    }

    //! Opens the existing file mapping for the shared memory segment
    void open(const wchar_t* name)
    {
        BOOST_ASSERT(m_handle == NULL);

        // Note: FILE_MAP_WRITE implies reading permission as well
        boost::detail::winapi::HANDLE_ h = boost::detail::winapi::OpenFileMappingW(boost::detail::winapi::FILE_MAP_WRITE_ | SECTION_QUERY_, false, name);

        if (BOOST_UNLIKELY(h == NULL))
        {
            const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to create a shared memory segment", (err));
        }

        try
        {
            m_size = obtain_size(h);
        }
        catch (...)
        {
            boost::detail::winapi::CloseHandle(h);
            throw;
        }

        m_handle = h;
    }

    //! Maps the file mapping into the current process memory
    void map()
    {
        BOOST_ASSERT(m_handle != NULL);
        BOOST_ASSERT(m_mapped_address == NULL);

        // Note: FILE_MAP_WRITE implies reading permission as well
        m_mapped_address = boost::detail::winapi::MapViewOfFile
        (
            m_handle,
            boost::detail::winapi::FILE_MAP_WRITE_ | SECTION_QUERY_,
            0u,
            0u,
            m_size
        );

        if (BOOST_UNLIKELY(m_mapped_address == NULL))
        {
            const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to map the shared memory segment into the process address space", (err));
        }
    }

    //! Unmaps the file mapping
    void unmap()
    {
        BOOST_ASSERT(m_mapped_address == NULL);

        BOOST_VERIFY(boost::detail::winapi::UnmapViewOfFile(m_mapped_address) != 0);
        m_mapped_address = NULL;
    }

    //! Returns the size of the opened shared memory segment
    std::size_t size() const BOOST_NOEXCEPT { return m_size; }
    //! Returns the address of the mapped shared memory
    void* address() const BOOST_NOEXCEPT { return m_mapped_address; }

    BOOST_DELETED_FUNCTION(windows_shared_memory(windows_shared_memory const&))
    BOOST_DELETED_FUNCTION(windows_shared_memory& operator=(windows_shared_memory const&))

private:
    //! Returns the size of the file mapping identified by the handle
    static std::size_t obtain_size(boost::detail::winapi::HANDLE_ h)
    {
        nt_query_section_t query_section = nt_query_section.load(boost::memory_order_acquire);

        if (BOOST_UNLIKELY(query_section == NULL))
        {
            // Check if ntdll.dll provides NtQuerySection, see: http://undocumented.ntinternals.net/index.html?page=UserMode%2FUndocumented%20Functions%2FNT%20Objects%2FSection%2FNtQuerySection.html
            boost::detail::winapi::HMODULE_ ntdll = boost::detail::winapi::GetModuleHandleW(L"ntdll.dll");
            if (BOOST_UNLIKELY(ntdll == NULL))
            {
                const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
                BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to obtain a handle to ntdll.dll", (err));
            }

            query_section = (nt_query_section_t)boost::detail::winapi::get_proc_address(ntdll, "NtQuerySection");
            if (BOOST_UNLIKELY(query_section == NULL))
            {
                const boost::detail::winapi::DWORD_ err = boost::detail::winapi::GetLastError();
                BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to obtain the NtQuerySection function", (err));
            }

            nt_query_section.store(query_section, boost::memory_order_release);
        }

        section_basic_information info = {};
        NTSTATUS_ err = query_section
        (
            h,
            0u, // SectionBasicInformation
            &info,
            sizeof(info),
            NULL
        );
        if (BOOST_UNLIKELY(err != 0u))
        {
            BOOST_LOG_THROW_DESCR_PARAMS(boost::log::system_error, "Failed to test obtain size of the shared memory segment", (ERROR_INVALID_HANDLE));
        }

        return info.section_size.QuadPart;
    }
};

boost::atomic< windows_shared_memory::nt_query_section_t > windows_shared_memory::nt_query_section(static_cast< windows_shared_memory::nt_query_section_t >(NULL));
const boost::detail::winapi::DWORD_ windows_shared_memory::SECTION_QUERY_;

} // namespace aux

} // namespace ipc

BOOST_LOG_CLOSE_NAMESPACE // namespace log

} // namespace boost

#include <boost/log/detail/footer.hpp>

#endif // BOOST_LOG_WINDOWS_SHARED_MEMORY_HPP_INCLUDED_
