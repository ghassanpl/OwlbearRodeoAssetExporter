#include "mmap.h"

namespace ghassanpl
{
#if defined(_WIN32) && !defined(_WINDOWS_)
  extern "C" __declspec(dllimport) int __stdcall FlushViewOfFile(void const* lpBaseAddress, size_t dwNumberOfBytesToFlush);
  extern "C" __declspec(dllimport) int __stdcall FlushFileBuffers(void* hFile);
  extern "C" __declspec(dllimport) int __stdcall UnmapViewOfFile(void const* lpBaseAddress);
  extern "C" __declspec(dllimport) int __stdcall CloseHandle(void* hObject);
  extern "C" __declspec(dllimport) unsigned long __stdcall GetLastError();
  extern "C" __declspec(dllimport) void* __stdcall CreateFileW(const wchar_t*, unsigned long, unsigned long, void*, unsigned long, unsigned long, void*);

  extern "C" struct SYSTEM_INFO {
    union {
      unsigned long dwOemId;          // Obsolete field...do not use
      struct {
        unsigned short wProcessorArchitecture;
        unsigned short wReserved;
      } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    unsigned long dwPageSize;
    void* lpMinimumApplicationAddress;
    void* lpMaximumApplicationAddress;
    unsigned long* dwActiveProcessorMask;
    unsigned long dwNumberOfProcessors;
    unsigned long dwProcessorType;
    unsigned long dwAllocationGranularity;
    unsigned short wProcessorLevel;
    unsigned short wProcessorRevision;
  };

  extern "C" __declspec(dllimport) void __stdcall GetSystemInfo(SYSTEM_INFO * lpSystemInfo);
  extern "C" __declspec(dllimport) void* __stdcall CreateFileMappingW(void* hFile, void* lpFileMappingAttributes, unsigned long flProtect, unsigned long dwMaximumSizeHigh, unsigned long dwMaximumSizeLow, const wchar_t* lpName);
  extern "C" __declspec(dllimport) void* __stdcall MapViewOfFile(void* hFileMappingObject, unsigned long dwDesiredAccess, unsigned long dwFileOffsetHigh, unsigned long dwFileOffsetLow, size_t dwNumberOfBytesToMap);
#endif

  namespace
  {
    inline std::error_code last_error() noexcept
    {
      std::error_code error;
#ifdef _WIN32
      error.assign(GetLastError(), std::system_category());
#else
      error.assign(errno, std::system_category());
#endif
      return error;
    }

    inline size_t page_size() noexcept
    {
      static const size_t page_size = []
      {
#ifdef _WIN32
        SYSTEM_INFO SystemInfo;
        GetSystemInfo(&SystemInfo);
        return SystemInfo.dwAllocationGranularity;
#else
        return sysconf(_SC_PAGE_SIZE);
#endif
      }();
      return page_size;
    }

    inline size_t make_offset_page_aligned(size_t offset) noexcept
    {
      const size_t page_size_ = page_size();
      // Use integer division to round down to the nearest page alignment.
      return offset / page_size_ * page_size_;
    }

    inline unsigned long int64_high(int64_t n) noexcept
    {
      return n >> 32;
    }

    inline unsigned long int64_low(int64_t n) noexcept
    {
      return n & 0xffffffff;
    }
  }

  void mmap_sink::sync(std::error_code& error) noexcept
  {
    error.clear();
    if (!is_open())
    {
      error = std::make_error_code(std::errc::bad_file_descriptor);
      return;
    }

    if (data())
    {
#ifdef _WIN32
      if (FlushViewOfFile(get_mapping_start(), mapped_length_) == 0 || FlushFileBuffers(file_handle_) == 0)
#else // POSIX
      if (::msync(get_mapping_start(), mapped_length_, MS_SYNC) != 0)
#endif
      {
        error = last_error();
        return;
      }
    }

#ifdef _WIN32
    if (FlushFileBuffers(file_handle_) == 0)
    {
      error = last_error();
    }
#endif
  }

  file_handle_type mmap_sink::open_file(const path& path, std::error_code& error) noexcept
  {
    if (path.empty())
    {
      error = std::make_error_code(std::errc::invalid_argument);
      return invalid_handle;
    }

#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), (0x80000000L) | (0x40000000L), 0x00000001 | 0x00000002, 0, 3, 0x00000080, 0);
#else // POSIX
    const auto handle = ::open(c_str(path), O_RDWR);
#endif
    if (handle == invalid_handle)
      error = last_error();

    return handle;
  }

  mmap_sink::mmap_context mmap_sink::memory_map(const file_handle_type file_handle, const int64_t offset, const int64_t length, std::error_code& error) noexcept
  {
    const int64_t aligned_offset = make_offset_page_aligned(offset);
    const int64_t length_to_map = offset - aligned_offset + length;
#ifdef _WIN32
    const int64_t max_file_size = offset + length;
    const auto file_mapping_handle = CreateFileMappingW(file_handle, 0, 0x04, int64_high(max_file_size), int64_low(max_file_size), 0);
    if (file_mapping_handle == invalid_handle)
    {
      error = last_error();
      return {};
    }
    char* mapping_start = static_cast<char*>(MapViewOfFile(file_mapping_handle, 0x0002, int64_high(aligned_offset), int64_low(aligned_offset), length_to_map));
    if (mapping_start == nullptr)
    {
      CloseHandle(file_mapping_handle);
      error = last_error();
      return {};
    }
#else // POSIX
    char* mapping_start = static_cast<char*>(::mmap(0, length_to_map, PROT_WRITE, MAP_SHARED, file_handle, aligned_offset));
    if (mapping_start == MAP_FAILED)
    {
      error = last_error();
      return {};
    }
#endif

    mmap_context ctx{};
    ctx.data = mapping_start + offset - aligned_offset;
    ctx.length = length;
    ctx.mapped_length = length_to_map;
    ctx.file_mapping_handle = file_mapping_handle;
    return ctx;
  }

  void mmap_source::unmap() noexcept
  {
    if (!is_open()) { return; }
    // TODO do we care about errors here?
#ifdef _WIN32
    if (is_mapped())
    {
      UnmapViewOfFile(get_mapping_start());
      CloseHandle(file_mapping_handle_);
    }
#else // POSIX
    if (data_) { ::munmap(const_cast<pointer>(get_mapping_start()), mapped_length_); }
#endif

#ifdef _WIN32
    CloseHandle(file_handle_);
#else // POSIX
    ::close(file_handle_);
#endif

    // Reset fields to their default values.
    data_ = nullptr;
    length_ = mapped_length_ = 0;
    file_handle_ = invalid_handle;
    file_mapping_handle_ = invalid_handle;
  }
  
  file_handle_type mmap_source::open_file(const path& path, std::error_code& error) noexcept
  {
    error.clear();
    if (path.empty())
    {
      error = std::make_error_code(std::errc::invalid_argument);
      return invalid_handle;
    }
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), (0x80000000L), 0x00000001 | 0x00000002, 0, 3, 0x00000080, 0);
#else // POSIX
    const auto handle = ::open(c_str(path), O_RDONLY);
#endif
    if (handle == invalid_handle)
    {
      error = last_error();
    }
    return handle;
  }

  mmap_source::mmap_context mmap_source::memory_map(const file_handle_type file_handle, const int64_t offset, const int64_t length, std::error_code& error) noexcept
  {
    const int64_t aligned_offset = make_offset_page_aligned(offset);
    const int64_t length_to_map = offset - aligned_offset + length;
#ifdef _WIN32
    const int64_t max_file_size = offset + length;
    const auto file_mapping_handle = CreateFileMappingW(file_handle, 0, 0x02, int64_high(max_file_size), int64_low(max_file_size), 0);
    if (file_mapping_handle == invalid_handle)
    {
      error = last_error();
      return {};
    }
    char* mapping_start = static_cast<char*>(MapViewOfFile(file_mapping_handle, 0x0004, int64_high(aligned_offset), int64_low(aligned_offset), length_to_map));
    if (mapping_start == nullptr)
    {
      // Close file handle if mapping it failed.
      CloseHandle(file_mapping_handle);
      error = last_error();
      return {};
    }
#else // POSIX
    char* mapping_start = static_cast<char*>(::mmap(0, length_to_map, PROT_READ, MAP_SHARED, file_handle, aligned_offset));
    if (mapping_start == MAP_FAILED)
    {
      error = last_error();
      return {};
    }
#endif

    mmap_context ctx{};
    ctx.data = mapping_start + offset - aligned_offset;
    ctx.length = length;
    ctx.mapped_length = length_to_map;
    ctx.file_mapping_handle = file_mapping_handle;
    return ctx;
  }
}