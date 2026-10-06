#include "archive_crypto.h"
#include "app_version.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <climits>
#include <memory>

namespace {
constexpr std::string_view Magic = "YUSIENC2";
constexpr int SaltSize = 16, NonceSize = 12, TagSize = 16;
constexpr int BaseHeaderSize = 8 + SaltSize + NonceSize;
constexpr int Iterations = 600000;
struct Header {
    int size = BaseHeaderSize;
    int saltOffset = 8;
    std::string_view creatorVersion;
};

bool startsWith(std::span<const unsigned char> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && std::equal(magic.begin(), magic.end(), bytes.begin());
}

bool readHeader(std::span<const unsigned char> bytes, Header &header) {
    if (!startsWith(bytes, Magic) || bytes.size() <= Magic.size()) return false;
    const int versionSize = bytes[Magic.size()];
    if (versionSize == 0 || versionSize > int(ArchiveCrypto::MaxCreatorVersionSize)
        || bytes.size() < size_t(BaseHeaderSize + 1 + versionSize + TagSize)) return false;
    header.creatorVersion = {reinterpret_cast<const char *>(bytes.data() + Magic.size() + 1), size_t(versionSize)};
    for (unsigned char c : header.creatorVersion) if (c < 0x21 || c > 0x7e) return false;
    header.size += 1 + versionSize;
    header.saltOffset += 1 + versionSize;
    return bytes.size() >= size_t(header.size + TagSize)
        && bytes.size() - header.size - TagSize <= ArchiveCrypto::MaxPayloadSize;
}

struct Key {
    std::array<unsigned char, 32> bytes{};
    ~Key() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
};
using Context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool deriveKey(std::string_view password, const unsigned char *salt, Key &key) {
    if (password.size() > INT_MAX) return false;
    return PKCS5_PBKDF2_HMAC(password.empty() ? "" : password.data(), int(password.size()),
        salt, SaltSize, Iterations, EVP_sha256(), int(key.bytes.size()), key.bytes.data()) == 1;
}

bool fail(std::string &error, const char *message) { error = message; return false; }
}

bool ArchiveCrypto::isEncrypted(std::span<const unsigned char> bytes) {
    return startsWith(bytes, Magic);
}

std::string ArchiveCrypto::creatorVersion(std::span<const unsigned char> bytes) {
    Header header;
    return readHeader(bytes, header) ? std::string(header.creatorVersion) : std::string();
}

bool ArchiveCrypto::encrypt(std::span<const unsigned char> plaintext, std::string_view password,
                            Bytes &result, std::string &error) {
    result.clear();
    error.clear();
    if (plaintext.size() > MaxPayloadSize) return fail(error, "The export exceeds 16 MiB.");
    constexpr std::string_view version = YusiManagerVersion;
    static_assert(!version.empty() && version.size() <= MaxCreatorVersionSize);
    constexpr int headerSize = BaseHeaderSize + 1 + version.size();
    Bytes encrypted(headerSize + plaintext.size() + EVP_MAX_BLOCK_LENGTH + TagSize);
    std::copy(Magic.begin(), Magic.end(), encrypted.begin());
    encrypted[Magic.size()] = static_cast<unsigned char>(version.size());
    std::copy(version.begin(), version.end(), encrypted.begin() + Magic.size() + 1);
    auto *salt = encrypted.data() + Magic.size() + 1 + version.size();
    auto *nonce = salt + SaltSize;
    if (RAND_bytes(salt, SaltSize) != 1 || RAND_bytes(nonce, NonceSize) != 1)
        return fail(error, "Could not generate secure random values.");
    Key key;
    if (!deriveKey(password, salt, key)) return fail(error, "Could not derive the encryption key.");
    Context ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    int count = 0, finalCount = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NonceSize, nullptr) != 1
        || EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.bytes.data(), nonce) != 1
        || EVP_EncryptUpdate(ctx.get(), nullptr, &count, encrypted.data(), headerSize) != 1
        || EVP_EncryptUpdate(ctx.get(), encrypted.data() + headerSize, &count, plaintext.data(), int(plaintext.size())) != 1
        || EVP_EncryptFinal_ex(ctx.get(), encrypted.data() + headerSize + count, &finalCount) != 1)
        return fail(error, "Could not encrypt the export.");
    const int encryptedSize = count + finalCount;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TagSize, encrypted.data() + headerSize + encryptedSize) != 1)
        return fail(error, "Could not authenticate the export.");
    encrypted.resize(headerSize + encryptedSize + TagSize);
    result = std::move(encrypted);
    return true;
}

bool ArchiveCrypto::decrypt(std::span<const unsigned char> archive, std::string_view password,
                            Bytes &result, std::string &error) {
    result.clear();
    error.clear();
    Header header;
    if (!readHeader(archive, header))
        return fail(error, "Invalid or unsupported encrypted .yusi file.");
    const auto *salt = archive.data() + header.saltOffset;
    const auto *nonce = salt + SaltSize;
    const int encryptedSize = int(archive.size() - header.size - TagSize);
    Key key;
    if (!deriveKey(password, salt, key)) return fail(error, "Could not derive the decryption key.");
    Context ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    Bytes plaintext(encryptedSize + EVP_MAX_BLOCK_LENGTH);
    int count = 0, finalCount = 0;
    std::array<unsigned char, TagSize> tag{};
    std::copy(archive.end() - TagSize, archive.end(), tag.begin());
    const bool success = ctx
        && EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, NonceSize, nullptr) == 1
        && EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.bytes.data(), nonce) == 1
        && EVP_DecryptUpdate(ctx.get(), nullptr, &count, archive.data(), header.size) == 1
        && EVP_DecryptUpdate(ctx.get(), plaintext.data(), &count, archive.data() + header.size, encryptedSize) == 1
        && EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TagSize, tag.data()) == 1
        && EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + count, &finalCount) == 1;
    if (!success) {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        return fail(error, "Incorrect password or damaged encrypted file. Nothing was imported.");
    }
    plaintext.resize(count + finalCount);
    result = std::move(plaintext);
    return true;
}
