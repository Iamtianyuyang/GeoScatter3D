#include "foundation/io/Sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef GS3D_USE_OPENSSL
#include <memory>
#include <openssl/evp.h>
#endif

namespace gs3d::foundation {
namespace {

#ifdef GS3D_USE_OPENSSL
class Sha256 {
public:
    Sha256() : context_(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
        if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
            throw std::runtime_error("sha256: cannot initialize digest");
        }
    }

    void update(const std::uint8_t* data, std::size_t size) {
        if (EVP_DigestUpdate(context_.get(), data, size) != 1) {
            throw std::runtime_error("sha256: cannot update digest");
        }
    }

    Sha256Digest finish() {
        Sha256Digest digest{};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1 || size != digest.size()) {
            throw std::runtime_error("sha256: cannot finalize digest");
        }
        return digest;
    }

private:
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
};
#else
constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

constexpr std::uint32_t rotate_right(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

class Sha256 {
public:
    void update(const std::uint8_t* data, std::size_t size) {
        total_bytes_ += size;
        while (size > 0) {
            const std::size_t take = std::min(size, block_.size() - used_);
            std::memcpy(block_.data() + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == block_.size()) {
                compress(block_.data());
                used_ = 0;
            }
        }
    }

    Sha256Digest finish() {
        const std::uint64_t bit_length = total_bytes_ * 8u;
        const std::uint8_t pad = 0x80;
        update(&pad, 1);
        const std::uint8_t zero = 0x00;
        while (used_ != 56) {
            update(&zero, 1);
        }
        std::uint8_t length_bytes[8];
        for (int i = 0; i < 8; ++i) {
            length_bytes[i] = static_cast<std::uint8_t>(bit_length >> (56 - 8 * i));
        }
        update(length_bytes, 8);

        Sha256Digest digest{};
        for (std::size_t i = 0; i < 8; ++i) {
            digest[4 * i + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
            digest[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
            digest[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
            digest[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    void compress(const std::uint8_t* block) {
        std::uint32_t words[64];
        for (int i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(block[4 * i]) << 24) |
                       (static_cast<std::uint32_t>(block[4 * i + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[4 * i + 2]) << 8) |
                       static_cast<std::uint32_t>(block[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotate_right(words[i - 15], 7) ^
                                     rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
            const std::uint32_t s1 = rotate_right(words[i - 2], 17) ^
                                     rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + words[i];
            const std::uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t total_bytes_ = 0;
};
#endif

} // namespace

std::string to_hex(const Sha256Digest& digest) {
    static const char* kDigits = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0f]);
    }
    return text;
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("sha256: cannot open " + path.string());
    }
    Sha256 hasher;
    std::vector<std::uint8_t> buffer(1u << 20);
    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()),
                    static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = stream.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (stream.bad()) {
        throw std::runtime_error("sha256: cannot read " + path.string());
    }
    return to_hex(hasher.finish());
}

} // namespace gs3d::foundation
