#pragma once
// Deterministic persistence-state mutation for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/digest.h"
#include "chaoslab/result.h"

#include <cstdint>
#include <string>

namespace chaoslab {

enum class MutationKind {
  TRUNCATE,           // cut the file at a byte offset
  CORRUPT_BYTES,      // XOR a range of bytes
  APPEND_GARBAGE,     // append N garbage bytes
  OVERWRITE_MAGIC,    // overwrite the first 4 bytes with 0xDEAD
  OVERWRITE_VERSION,  // overwrite a version field at an offset
  ZERO_RANGE,         // zero a byte range
  WRITE_PARTIAL_TEMP, // write an incomplete temp artifact and leave it
  Last
};

const char* mutation_kind_name(MutationKind k) noexcept;

struct PersistenceMutation {
  MutationKind kind{MutationKind::TRUNCATE};
  std::uint64_t offset{0};
  std::uint64_t length{0};
};

/// Compute the SHA-256 digest of a file.
Status file_digest(const std::string& path, Digest256& out);

/// Copy src to dst. Creates/truncates dst.
Status copy_file(const std::string& src, const std::string& dst);

/// Whether a file exists.
bool file_exists(const std::string& path);

/// Apply a deterministic mutation to a file. The file is modified in place; the
/// caller is responsible for operating on a campaign-owned disposable copy.
Status apply_mutation(const std::string& path, const PersistenceMutation& m);

/// Write a partial temp artifact (bytes0..N-1) to p path without a rename.
Status write_partial_temp(const std::string& path, const std::uint8_t* bytes, std::size_t n);

/// Move src -> dst via rename.
Status rename_file(const std::string& src, const std::string& dst);

} // namespace chaoslab
