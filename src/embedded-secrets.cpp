#include "embedded-secrets.hpp"

#include <cstring>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace {

struct EmbeddedBlob {
	const unsigned char *cipher;
	size_t size;
	unsigned char iv[16];
	unsigned char keyA[32];
	unsigned char keyB[32]; // key = keyA ^ keyB
};

#include "embedded-secrets.inc"

std::string decrypt(const EmbeddedBlob &blob)
{
	if (!blob.cipher || blob.size == 0)
		return {};
	unsigned char key[32], iv[16];
	for (int i = 0; i < 32; i++)
		key[i] = blob.keyA[i] ^ blob.keyB[i];
	memcpy(iv, blob.iv, sizeof(iv));

	std::string result;
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_KEY_HANDLE handle = nullptr;
	if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)) &&
	    BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
					     sizeof(BCRYPT_CHAIN_MODE_CBC), 0)) &&
	    BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, key, sizeof(key), 0))) {
		std::vector<unsigned char> plain(blob.size);
		ULONG len = 0;
		if (BCRYPT_SUCCESS(BCryptDecrypt(handle, (PUCHAR)blob.cipher, (ULONG)blob.size, nullptr, iv, sizeof(iv),
						 plain.data(), (ULONG)plain.size(), &len, BCRYPT_BLOCK_PADDING)))
			result.assign((const char *)plain.data(), len);
		SecureZeroMemory(plain.data(), plain.size());
	}
	if (handle)
		BCryptDestroyKey(handle);
	if (alg)
		BCryptCloseAlgorithmProvider(alg, 0);
	SecureZeroMemory(key, sizeof(key));
	return result;
}

} // namespace

std::string embedded_secret(Secret which)
{
	switch (which) {
	case Secret::FeedbackUrl:
		return decrypt(kFeedbackUrl);
	case Secret::FeedbackKey:
		return decrypt(kFeedbackKey);
	}
	return {};
}
