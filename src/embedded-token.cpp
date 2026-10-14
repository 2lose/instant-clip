#include "embedded-token.hpp"

#include <cstring>
#include <vector>

#include <windows.h>
#include <bcrypt.h>

namespace {
#include "update-token.inc"
}

// AES-256-CBC, key split in two arrays; see scripts/embed-token.ps1.
std::string embedded_token()
{
	if (kTokCipherLen == 0)
		return {};
	unsigned char key[32], iv[16];
	for (int i = 0; i < 32; i++)
		key[i] = kTokKeyA[i] ^ kTokKeyB[i];
	memcpy(iv, kTokIv, sizeof(iv));

	std::string result;
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_KEY_HANDLE handle = nullptr;
	if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0)) &&
	    BCRYPT_SUCCESS(BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
					     sizeof(BCRYPT_CHAIN_MODE_CBC), 0)) &&
	    BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg, &handle, nullptr, 0, key, sizeof(key), 0))) {
		std::vector<unsigned char> plain(kTokCipherLen);
		ULONG len = 0;
		if (BCRYPT_SUCCESS(BCryptDecrypt(handle, (PUCHAR)kTokCipher, (ULONG)kTokCipherLen, nullptr, iv,
						 sizeof(iv), plain.data(), (ULONG)plain.size(), &len, BCRYPT_BLOCK_PADDING)))
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

