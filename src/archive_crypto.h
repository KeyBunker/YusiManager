#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ArchiveCrypto {
using Bytes = std::vector<unsigned char>;
inline constexpr std::size_t MaxPayloadSize = 16 * 1024 * 1024;
inline constexpr std::size_t MaxCreatorVersionSize = 32;
inline constexpr std::size_t MaxOverhead = 52 + 1 + MaxCreatorVersionSize;
bool isEncrypted(std::span<const unsigned char> bytes);
std::string creatorVersion(std::span<const unsigned char> bytes);
bool encrypt(std::span<const unsigned char> plaintext, std::string_view password, Bytes &result, std::string &error);
bool decrypt(std::span<const unsigned char> archive, std::string_view password, Bytes &result, std::string &error);
}
