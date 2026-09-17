#pragma once

#include "netx/core/expected.hpp"
#include <array>
#include <cstdint>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/types.h>
#include <string>
#include <string_view>

namespace netx::websocket::details {
struct WSHandshake {
    /// RFC 6455 4.2.2：key + magic 取 SHA1 再 base64。
    /// 会失败的只有 BIO 链（分配不出来），一律按资源耗尽上报。
    static core::Expected<std::string>
    generate_accept_key(std::string_view client_key) {
        static constexpr std::string_view magic =
            "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        std::string combined{client_key};
        combined.append(magic);

        std::array<std::uint8_t, SHA_DIGEST_LENGTH> hash{};
        SHA1(reinterpret_cast<const std::uint8_t *>(combined.data()),
             combined.size(),
             hash.data());

        BIO *bio = BIO_new(BIO_f_base64());
        if (bio == nullptr) {
            return core::details::make_error_to_unexpected(
                core::details::Error::ResourceExhausted);
        }
        BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

        BIO *mem = BIO_new(BIO_s_mem());
        if (mem == nullptr) {
            BIO_free(bio);
            return core::details::make_error_to_unexpected(
                core::details::Error::ResourceExhausted);
        }
        bio = BIO_push(bio, mem);

        if (BIO_write(bio, hash.data(), hash.size()) != SHA_DIGEST_LENGTH ||
            BIO_flush(bio) != 1) {
            BIO_free_all(bio);
            return core::details::make_error_to_unexpected(
                core::details::Error::ResourceExhausted);
        }

        BUF_MEM *buffer_ptr = nullptr;
        BIO_get_mem_ptr(bio, &buffer_ptr);
        if (buffer_ptr == nullptr) {
            BIO_free_all(bio);
            return core::details::make_error_to_unexpected(
                core::details::Error::ResourceExhausted);
        }

        std::string result{buffer_ptr->data, buffer_ptr->length};
        BIO_free_all(bio);

        return result;
    }
};
} // namespace netx::websocket::details