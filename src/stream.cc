/*
 * libkeepass - KeePass key database importer/exporter
 * Copyright (C) 2014 Christian Kindahl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "stream.hh"

#include <openssl/sha.h>

namespace keepass {

std::array<uint8_t, 32> hashed_basic_streambuf::GetBlockHash() const {
  std::array<uint8_t, 32> block_hash;

  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, block_.data(), block_.size());
  SHA256_Final(block_hash.data(), &sha256);

  return block_hash;
}

int hashed_istreambuf::underflow() {
  static constexpr std::array<uint8_t, 32> kEmptyHash = { { 0 } };

  if (gptr() == egptr()) {
    BlockHeader header;
    src_.read(reinterpret_cast<char*>(&header), sizeof(BlockHeader));

    if (header.block_index != block_index_)
      throw std::runtime_error("block index mismatch.");
    block_index_++;

    block_.clear();
    std::generate_n(std::back_inserter(block_),
                    header.block_size,
                    [&]() { return src_.get(); });

    if (header.block_size == 0) {
      if (header.block_hash != kEmptyHash)
        throw std::runtime_error("corrupt end-of-stream block.");

      return std::char_traits<char>::eof();
    }

    // Verify the block integrity.
    if (GetBlockHash() != header.block_hash)
      throw std::runtime_error("block integrity failed.");

    setg(block_.data(), block_.data(), block_.data() + block_.size());
  }

  return gptr() == egptr() ?
      std::char_traits<char>::eof() :
      std::char_traits<char>::to_int_type(*gptr());
}

bool hashed_ostreambuf::FlushBlock() {
  static constexpr std::array<uint8_t, 32> kEmptyHash = { { 0 } };

  // Write block header and data.
  BlockHeader header;
  header.block_index = block_index_++;
  header.block_hash = block_.empty() ? kEmptyHash : GetBlockHash();
  header.block_size = static_cast<uint32_t>(block_.size());

  dst_.write(reinterpret_cast<const char*>(&header), sizeof(BlockHeader));
  dst_.write(block_.data(), block_.size());
  if (!dst_.good())
    return false;

  block_.clear();
  return true;
}

int hashed_ostreambuf::overflow(int c) {
  if (c == std::char_traits<char>::eof())
    return c;

  if (c > 0xff) {
    throw std::runtime_error(
        "internal error, trying to write multiple bytes to byte stream.");
  }

  block_.push_back(static_cast<char>(c));

  if (block_.size() == block_size_) {
    if (!FlushBlock())
      return std::char_traits<char>::eof();
  }

  return std::char_traits<char>::to_int_type(static_cast<char>(c));
}

int hashed_ostreambuf::sync() {
  if (!block_.empty()) {
    if (!FlushBlock())
      return -1;
  }

  // Write the trailing empty block.
  return FlushBlock() ? 0 : -1;
}

}   // namespace keepass