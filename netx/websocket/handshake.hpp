#pragma once

#include <cstdint>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/types.h>
#include <string>
namespace netx
{
namespace websocket
{
namespace details
{
struct WSHandshake
{
	static std::string generate_accept_key(const std::string& client_key)
	{
		static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		std::string combined = client_key + magic;

		std::uint8_t hash[SHA_DIGEST_LENGTH];
		SHA1(reinterpret_cast<const std::uint8_t*>(combined.data()),
			 combined.size(), hash);

		BIO* bio = nullptr;
		BIO* b64 = nullptr;
		BUF_MEM* buffer_ptr = nullptr;

		b64 = BIO_new(BIO_f_base64());
		bio = BIO_new(BIO_s_mem());
		BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
		bio = BIO_push(b64, bio);

		BIO_write(bio, hash, SHA_DIGEST_LENGTH);
		BIO_flush(bio);
		BIO_get_mem_ptr(bio, &buffer_ptr);

		std::string result(buffer_ptr->data, buffer_ptr->length);
		BIO_free_all(bio);

		return result;
	}
};
} // namespace details
} // namespace websocket
} // namespace netx