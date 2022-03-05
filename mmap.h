/// This Source Code Form is subject to the terms of the Mozilla Public
/// License, v. 2.0. If a copy of the MPL was not distributed with this
/// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <iterator>
#include <string>
#include <system_error>
#include <cstdint>
#include <span>
#include <filesystem>

namespace ghassanpl
{
  static constexpr size_t map_entire_file = 0;

#ifdef _WIN32
  using file_handle_type = void*;
#else
  using file_handle_type = int;
#endif

  static inline const file_handle_type invalid_handle = (file_handle_type)-1;
  
  template <typename CRTP>
  struct basic_mmap
  {
    using value_type = std::byte;
    using size_type = size_t;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using difference_type = std::ptrdiff_t;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using iterator_category = std::random_access_iterator_tag;
    using handle_type = file_handle_type;
    using path = std::filesystem::path;

    basic_mmap() = default;
    basic_mmap(const path& path, const size_type offset = 0, const size_type length = map_entire_file)
    {
      std::error_code error;
      map(path, offset, length, error);
      if (error) { throw std::system_error(error); }
    }
    basic_mmap(const handle_type handle, const size_type offset = 0, const size_type length = map_entire_file)
    {
      std::error_code error;
      map(handle, offset, length, error);
      if (error) { throw std::system_error(error); }
    }

    basic_mmap(const basic_mmap&) = delete;
    basic_mmap(basic_mmap&& other) noexcept
      : data_(std::exchange(other.data_, nullptr))
      , length_(std::exchange(other.length_, 0))
      , mapped_length_(std::exchange(other.mapped_length_, 0))
      , file_handle_(std::exchange(other.file_handle_, invalid_handle))
      , file_mapping_handle_(std::exchange(other.file_mapping_handle_, invalid_handle))
    {
    }
    basic_mmap& operator=(const basic_mmap&) = delete;
    basic_mmap& operator=(basic_mmap&& other) noexcept
    {
      if (this != &other)
      {
        unmap();

        data_ = std::exchange(other.data_, nullptr);
        length_ = std::exchange(other.length_, 0);
        mapped_length_ = std::exchange(other.mapped_length_, 0);
        file_handle_ = std::exchange(other.file_handle_, invalid_handle);
        file_mapping_handle_ = std::exchange(other.file_mapping_handle_, invalid_handle);
      }
      return *this;
    }

    ~basic_mmap() noexcept
    {
      static_cast<CRTP*>(this)->conditional_sync();
      unmap();
    }

    handle_type file_handle() const noexcept { return file_handle_; }
    handle_type mapping_handle() const noexcept { return file_mapping_handle_ == invalid_handle ? file_handle_ : file_mapping_handle_; }

    bool is_open() const noexcept { return file_handle_ != invalid_handle; }

    bool empty() const noexcept { return length() == 0; }

    bool is_mapped() const noexcept
    {
#ifdef _WIN32
      return file_mapping_handle_ != invalid_handle;
#else // POSIX
      return is_open();
#endif
    }

    size_type size() const noexcept { return length(); }
    size_type length() const noexcept { return length_; }
    size_type mapped_length() const noexcept { return mapped_length_; }

    size_type mapping_offset() const noexcept
    {
      return mapped_length_ - length_;
    }

    const_pointer data() const noexcept { return data_; }

    const_iterator begin() const noexcept { return data(); }
    const_iterator cbegin() const noexcept { return data(); }

    const_iterator end() const noexcept { return data() + length(); }
    const_iterator cend() const noexcept { return data() + length(); }

    const_reverse_iterator rbegin() const noexcept
    {
      return const_reverse_iterator(end());
    }

    const_reverse_iterator crbegin() const noexcept
    {
      return const_reverse_iterator(end());
    }

    const_reverse_iterator rend() const noexcept
    {
      return const_reverse_iterator(begin());
    }
    const_reverse_iterator crend() const noexcept
    {
      return const_reverse_iterator(begin());
    }

    const_reference operator[](const size_type i) const noexcept { return data_[i]; }

    void map(const path& path, const size_type offset, const size_type length, std::error_code& error) noexcept
    {
      error.clear();
      if (path.empty())
      {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
      }

      error.clear();
      const auto file_size = std::filesystem::file_size(path, error);
      if (error)
        return;

      error.clear();
      const auto handle = static_cast<CRTP*>(this)->open_file(path, error);
      if (error)
        return;
      
      error.clear();
      if (handle == invalid_handle)
      {
        error = std::make_error_code(std::errc::bad_file_descriptor);
        return;
      }

      if (offset + length > file_size)
      {
        error = std::make_error_code(std::errc::invalid_argument);
        return;
      }

      const auto ctx = static_cast<CRTP*>(this)->memory_map(handle, offset, length == map_entire_file ? (file_size - offset) : length, error);
      if (!error)
      {
        // We must unmap the previous mapping that may have existed prior to this call.
        // Note that this must only be invoked after a new mapping has been created in
        // order to provide the strong guarantee that, should the new mapping fail, the
        // `map` function leaves this instance in a state as though the function had
        // never been invoked.
        unmap();

        file_handle_ = handle;
        data_ = reinterpret_cast<pointer>(ctx.data);
        length_ = ctx.length;
        mapped_length_ = ctx.mapped_length;
        file_mapping_handle_ = ctx.file_mapping_handle;
      }
    }

    void map(const path& path, std::error_code& error) noexcept
    {
      map(path, 0, map_entire_file, error);
    }

    void unmap() noexcept
    {
      static_cast<CRTP*>(this)->unmap();
    }

    void swap(basic_mmap& other) noexcept
    {
      if (this != &other)
      {
        using std::swap;
        swap(data_, other.data_);
        swap(file_handle_, other.file_handle_);
        swap(file_mapping_handle_, other.file_mapping_handle_);
        swap(length_, other.length_);
        swap(mapped_length_, other.mapped_length_);
      }
    }

  protected:

    struct mmap_context
    {
      char* data;
      int64_t length;
      int64_t mapped_length;
      file_handle_type file_mapping_handle;
    };

    pointer data_ = nullptr;

    size_type length_ = 0;
    size_type mapped_length_ = 0;

    handle_type file_handle_ = invalid_handle;
    handle_type file_mapping_handle_ = invalid_handle;

    const_pointer get_mapping_start() const noexcept
    {
      return !data() ? nullptr : data() - mapping_offset();
    }

  };

  struct mmap_source : public basic_mmap<mmap_source>
  {
    using basic_mmap::basic_mmap;

    void unmap() noexcept;

  protected:

    friend struct basic_mmap;

    static file_handle_type open_file(const path& path, std::error_code& error) noexcept;

    static mmap_context memory_map(const file_handle_type file_handle, const int64_t offset, const int64_t length, std::error_code& error) noexcept;

    void conditional_sync() {}
  };

  struct mmap_sink : public basic_mmap<mmap_sink>
  {
    using basic_mmap::basic_mmap;

    using basic_mmap::operator[];
    reference operator[](const size_type i) noexcept { return data_[i]; }

    void unmap();

    using basic_mmap::data;
    pointer data() noexcept { return data_; }
    
    using basic_mmap::begin;
    iterator begin() noexcept { return data(); }

    using basic_mmap::end;
    iterator end() noexcept { return data() + length(); }

    using basic_mmap::rbegin;
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

    using basic_mmap::rend;
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

    void sync(std::error_code& error) noexcept;

  protected:

    friend struct basic_mmap;

    static file_handle_type open_file(const path& path, std::error_code& error) noexcept;

    static mmap_context memory_map(const file_handle_type file_handle, const int64_t offset, const int64_t length, std::error_code& error) noexcept;

    pointer get_mapping_start() noexcept
    {
      return !data() ? nullptr : data() - mapping_offset();
    }

    void conditional_sync()
    {
      std::error_code ec;
      sync(ec);
    }
  };


  inline mmap_source make_mmap_source(const std::filesystem::path& path, mmap_source::size_type offset, mmap_source::size_type length, std::error_code& error) noexcept
  {
    mmap_source mmap;
    mmap.map(path, offset, length, error);
    return mmap;
  }

  inline mmap_source make_mmap_source(const std::filesystem::path& path, std::error_code& error) noexcept
  {
    return make_mmap_source(path, 0, map_entire_file, error);
  }

  inline mmap_sink make_mmap_sink(const std::filesystem::path& path, mmap_sink::size_type offset, mmap_sink::size_type length, std::error_code& error) noexcept
  {
    mmap_sink mmap;
    mmap.map(path, offset, length, error);
    return mmap;
  }

  inline mmap_sink make_mmap_sink(const std::filesystem::path& path, std::error_code& error) noexcept
  {
    return make_mmap_sink(path, 0, map_entire_file, error);
  }

  inline mmap_source make_mmap_source(const std::filesystem::path& path, mmap_source::size_type offset, mmap_source::size_type length)
  {
    return mmap_source{ path, offset, length };
  }

  inline mmap_source make_mmap_source(const std::filesystem::path& path)
  {
    return make_mmap_source(path, 0, map_entire_file);
  }

  inline mmap_sink make_mmap_sink(const std::filesystem::path& path, mmap_sink::size_type offset, mmap_sink::size_type length)
  {
    return mmap_sink{ path, offset, length };
  }

  inline mmap_sink make_mmap_sink(const std::filesystem::path& path)
  {
    return make_mmap_sink(path, 0, map_entire_file);
  }

}