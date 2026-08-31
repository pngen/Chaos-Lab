#include "chaoslab/persistence.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace chaoslab {

namespace {
constexpr std::array<const char*, static_cast<std::size_t>(MutationKind::Last)> kNames = {
  "truncate","corrupt_bytes","append_garbage","overwrite_magic","overwrite_version",
  "zero_range","write_partial_temp"
};
} // namespace

const char* mutation_kind_name(MutationKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kNames.size() ? kNames[i] : "unknown";
}

bool file_exists(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return f.good();
}

Status file_digest(const std::string& path, Digest256& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Status::error(StatusCode::io_error, "cannot open file for digest: " + path);
  Sha256 h;
  std::vector<std::uint8_t> buf(64 * 1024);
  while (f) {
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    std::streamsize n = f.gcount();
    if (n > 0) h.update(buf.data(), static_cast<std::size_t>(n));
  }
  out = h.finish();
  return Status::ok();
}

Status copy_file(const std::string& src, const std::string& dst) {
  std::ifstream in(src, std::ios::binary);
  if (!in) return Status::error(StatusCode::io_error, "cannot open source: " + src);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  if (!out) return Status::error(StatusCode::io_error, "cannot open dest: " + dst);
  std::vector<char> buf(64 * 1024);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize n = in.gcount();
    if (n > 0) out.write(buf.data(), n);
  }
  out.flush();
  return out.good() ? Status::ok() : Status::error(StatusCode::io_error, "write failed: " + dst);
}

Status apply_mutation(const std::string& path, const PersistenceMutation& m) {
  std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
  if (!f) return Status::error(StatusCode::io_error, "cannot open for mutation: " + path);

  f.seekp(0, std::ios::end);
  std::uint64_t size = static_cast<std::uint64_t>(f.tellp());
  f.clear();

  switch (m.kind) {
    case MutationKind::TRUNCATE: {
      std::uint64_t cut = m.offset;
      if (cut > size) cut = size;
      f.close();
#ifdef _WIN32
      int fd = -1;
      errno_t e = _sopen_s(&fd, path.c_str(), _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
      if (e != 0 || fd < 0) return Status::error(StatusCode::io_error, "truncate open failed on " + path);
      int rc = _chsize_s(fd, static_cast<__int64>(cut));
      _close(fd);
      if (rc != 0) return Status::error(StatusCode::io_error, "truncate failed on " + path);
#else
      if (::truncate(path.c_str(), static_cast<std::streamoff>(cut)) != 0)
        return Status::error(StatusCode::io_error, "truncate failed on " + path);
#endif
      return Status::ok();
    }
    case MutationKind::CORRUPT_BYTES: {
      std::uint64_t start = m.offset;
      std::uint64_t len = m.length ? m.length : 1;
      if (start >= size) return Status::error(StatusCode::io_error, "corrupt offset beyond EOF");
      std::vector<char> buf(len < 64 ? len : 64);
      f.seekg(static_cast<std::streamoff>(start));
      f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
      std::streamsize got = f.gcount();
      for (std::streamsize i = 0; i < got; ++i) buf[static_cast<std::size_t>(i)] ^= static_cast<char>(0xa5);
      f.seekp(static_cast<std::streamoff>(start));
      f.write(buf.data(), got);
      return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "corrupt write failed");
    }
    case MutationKind::APPEND_GARBAGE: {
      std::uint64_t n = m.length ? m.length : 16;
      f.seekp(0, std::ios::end);
      std::uint8_t g = 0x7f;
      for (std::uint64_t i = 0; i < n; ++i) { f.write(reinterpret_cast<char*>(&g), 1); ++g; }
      return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "append failed");
    }
    case MutationKind::OVERWRITE_MAGIC: {
      if (size < 4) return Status::error(StatusCode::io_error, "file too small for magic");
      std::uint8_t dead[4] = {0xde, 0xad, 0xbe, 0xef};
      f.seekp(0);
      f.write(reinterpret_cast<const char*>(dead), 4);
      return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "magic write failed");
    }
    case MutationKind::OVERWRITE_VERSION: {
      if (m.offset + 4 > size) return Status::error(StatusCode::io_error, "version offset beyond EOF");
      char zero[4] = {0, 0, 0, 0};
      f.seekp(static_cast<std::streamoff>(m.offset));
      f.write(zero, 4);
      return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "version write failed");
    }
    case MutationKind::ZERO_RANGE: {
      if (m.offset >= size) return Status::error(StatusCode::io_error, "zero offset beyond EOF");
      std::uint64_t len = m.length; if (len == 0) len = 1;
      if (m.offset + len > size) len = size - m.offset;
      f.seekp(static_cast<std::streamoff>(m.offset));
      char z = 0;
      for (std::uint64_t i = 0; i < len; ++i) f.write(&z, 1);
      return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "zero write failed");
    }
    case MutationKind::WRITE_PARTIAL_TEMP: {
      // Present as truncate of the original (never rename) — the temp artifact
      // is what the caller writes separately.
      return Status::ok();
    }
    case MutationKind::Last:
      return Status::error(StatusCode::invalid_argument, "unknown mutation");
  }
  return Status::error(StatusCode::invalid_argument, "unknown mutation");
}

Status write_partial_temp(const std::string& path, const std::uint8_t* bytes, std::size_t n) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return Status::error(StatusCode::io_error, "cannot open temp: " + path);
  f.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(n));
  f.flush();
  return f.good() ? Status::ok() : Status::error(StatusCode::io_error, "partial temp write failed");
}

Status rename_file(const std::string& src, const std::string& dst) {
  if (std::rename(src.c_str(), dst.c_str()) != 0)
    return Status::error(StatusCode::io_error, "rename failed: " + src + " -> " + dst);
  return Status::ok();
}

} // namespace chaoslab
