/* algorithms.c - the algorithms supported by the rhash library
 *
 * Copyright: 2011-2012 Aleksey Kravchenko <rhash.admin@gmail.com>
 *
 * Permission is hereby granted,  free of charge,  to any person  obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction,  including without limitation
 * the rights to  use, copy, modify,  merge, publish, distribute, sublicense,
 * and/or sell copies  of  the Software,  and to permit  persons  to whom the
 * Software is furnished to do so.
 *
 * This program  is  distributed  in  the  hope  that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  Use this program  at  your own risk!
 */

#include <stdio.h>
#include <assert.h>

#include "byte_order.h"
#include "rhash.h"
#include "algorithms.h"

/* header files of all supported hash sums */
#include "aich.h"
#include "crc32.h"
#include "ed2k.h"
#include "edonr.h"
#include "gost94.h"
#include "has160.h"
#include "md4.h"
#include "md5.h"
#include "ripemd-160.h"
#include "snefru.h"
#include "sha1.h"
#include "sha256.h"
#include "sha512.h"
#include "sha3.h"
#include "tiger.h"
#include "torrent.h"
#include "tth.h"
#include "whirlpool.h"

#ifdef USE_OPENSSL
/* note: BTIH and AICH depends on the used SHA1 algorithm */
# define NEED_OPENSSL_INIT (RHASH_MD4 | RHASH_MD5 | \
	RHASH_SHA1 | RHASH_SHA224 | RHASH_SHA256 | RHASH_SHA384 | RHASH_SHA512 | \
	RHASH_BTIH | RHASH_AICH | RHASH_RIPEMD160 | RHASH_WHIRLPOOL)
#else
# define NEED_OPENSSL_INIT 0
#endif /* USE_OPENSSL */
#ifdef GENERATE_GOST94_LOOKUP_TABLE
# define NEED_GOST94_INIT (RHASH_GOST | RHASH_GOST_CRYPTOPRO)
#else
# define NEED_GOST94_INIT 0
#endif /* GENERATE_GOST94_LOOKUP_TABLE */

#define RHASH_NEED_INIT_ALG (NEED_GOST94_INIT | NEED_OPENSSL_INIT)
unsigned rhash_uninitialized_algorithms = RHASH_NEED_INIT_ALG;

rhash_hash_info* rhash_info_table = rhash_hash_info_default;
int rhash_info_size = RHASH_HASH_COUNT;

static void rhash_crc32_init(uint32_t* crc32);
static void rhash_crc32_update(uint32_t* crc32, const unsigned char* msg, size_t size);
static void rhash_crc32_final(uint32_t* crc32, unsigned char* result);
static void rhash_crc32c_init(uint32_t* crc32);
static void rhash_crc32c_update(uint32_t* crc32, const unsigned char* msg, size_t size);
static void rhash_crc32c_final(uint32_t* crc32, unsigned char* result);

rhash_info info_crc32  = { RHASH_CRC32,  F_BE32, 4, "CRC32", "crc32" };
rhash_info info_crc32c = { RHASH_CRC32C, F_BE32, 4, "CRC32C", "crc32c" };
rhash_info info_md4 = { RHASH_MD4, F_LE32, 16, "MD4", "md4" };
rhash_info info_md5 = { RHASH_MD5, F_LE32, 16, "MD5", "md5" };
rhash_info info_sha1 = { RHASH_SHA1,      F_BE32, 20, "SHA1", "sha1" };
rhash_info info_tiger = { RHASH_TIGER,    F_LE64, 24, "TIGER", "tiger" };
rhash_info info_tth  = { RHASH_TTH,       F_BS32, 24, "TTH", "tree:tiger" };
rhash_info info_btih = { RHASH_BTIH,      0, 20, "BTIH", "btih" };
rhash_info info_ed2k = { RHASH_ED2K,      F_LE32, 16, "ED2K", "ed2k" };
rhash_info info_aich = { RHASH_AICH,      F_BS32, 20, "AICH", "aich" };
rhash_info info_whirlpool = { RHASH_WHIRLPOOL, F_BE64, 64, "WHIRLPOOL", "whirlpool" };
rhash_info info_rmd160 = { RHASH_RIPEMD160,  F_LE32, 20, "RIPEMD-160", "ripemd160" };
rhash_info info_gost94 = { RHASH_GOST,       F_LE32, 32, "GOST", "gost" };
rhash_info info_gost94pro = { RHASH_GOST_CRYPTOPRO, F_LE32, 32, "GOST-CRYPTOPRO", "gost-cryptopro" };
rhash_info info_has160 = { RHASH_HAS160,     F_LE32, 20, "HAS-160", "has160" };
rhash_info info_snf128 = { RHASH_SNEFRU128,  F_BE32, 16, "SNEFRU-128", "snefru128" };
rhash_info info_snf256 = { RHASH_SNEFRU256,  F_BE32, 32, "SNEFRU-256", "snefru256" };
rhash_info info_sha224 = { RHASH_SHA224,     F_BE32, 28, "SHA-224", "sha224" };
rhash_info info_sha256 = { RHASH_SHA256,     F_BE32, 32, "SHA-256", "sha256" };
rhash_info info_sha384 = { RHASH_SHA384,     F_BE64, 48, "SHA-384", "sha384" };
rhash_info info_sha512 = { RHASH_SHA512,     F_BE64, 64, "SHA-512", "sha512" };
rhash_info info_edr256 = { RHASH_EDONR256,   F_LE32, 32, "EDON-R256", "edon-r256" };
rhash_info info_edr512 = { RHASH_EDONR512,   F_LE64, 64, "EDON-R512", "edon-r512" };
rhash_info info_sha3_224 = { RHASH_SHA3_224, F_LE64, 28, "SHA3-224", "sha3-224" };
rhash_info info_sha3_256 = { RHASH_SHA3_256, F_LE64, 32, "SHA3-256", "sha3-256" };
rhash_info info_sha3_384 = { RHASH_SHA3_384, F_LE64, 48, "SHA3-384", "sha3-384" };
rhash_info info_sha3_512 = { RHASH_SHA3_512, F_LE64, 64, "SHA3-512", "sha3-512" };

/* some helper macros */
#define dgshft(name) (((char*)&((name##_ctx*)0)->hash) - (char*)0)
#define dgshft2(name, field) (((char*)&((name##_ctx*)0)->field) - (char*)0)
#define ini(name) ((pinit_t)(name##_init))
#define upd(name) ((pupdate_t)(name##_update))
#define fin(name) ((pfinal_t)(name##_final))
#define iuf(name) ini(name), upd(name), fin(name)
#define diuf(name) dgshft(name), ini(name), upd(name), fin(name)

/* information about all supported hash functions */
rhash_hash_info rhash_hash_info_default[RHASH_HASH_COUNT] =
{
	{ &info_crc32, sizeof(uint32_t), 0, iuf(rhash_crc32), 0 }, /* 32 bit */
	{ &info_md4, sizeof(md4_ctx), dgshft(md4), iuf(rhash_md4), 0 }, /* 128 bit */
	{ &info_md5, sizeof(md5_ctx), dgshft(md5), iuf(rhash_md5), 0 }, /* 128 bit */
	{ &info_sha1, sizeof(sha1_ctx), dgshft(sha1), iuf(rhash_sha1), 0 }, /* 160 bit */
	{ &info_tiger, sizeof(tiger_ctx), dgshft(tiger), iuf(rhash_tiger), 0 }, /* 192 bit */
	{ &info_tth, sizeof(tth_ctx), dgshft2(tth, tiger.hash), iuf(rhash_tth), 0 }, /* 192 bit */
	{ &info_btih, sizeof(torrent_ctx), dgshft2(torrent, btih), iuf(bt), (pcleanup_t)bt_cleanup }, /* 160 bit */
	{ &info_ed2k, sizeof(ed2k_ctx), dgshft2(ed2k, md4_context_inner.hash), iuf(rhash_ed2k), 0 }, /* 128 bit */
	{ &info_aich, sizeof(aich_ctx), dgshft2(aich, sha1_context.hash), iuf(rhash_aich), (pcleanup_t)rhash_aich_cleanup }, /* 160 bit */
	{ &info_whirlpool, sizeof(whirlpool_ctx), dgshft(whirlpool), iuf(rhash_whirlpool), 0 }, /* 512 bit */
	{ &info_rmd160, sizeof(ripemd160_ctx), dgshft(ripemd160), iuf(rhash_ripemd160), 0 }, /* 160 bit */
	{ &info_gost94, sizeof(gost94_ctx), dgshft(gost94), iuf(rhash_gost94), 0 }, /* 256 bit */
	{ &info_gost94pro, sizeof(gost94_ctx), dgshft(gost94), ini(rhash_gost94_cryptopro), upd(rhash_gost94), fin(rhash_gost94), 0 }, /* 256 bit */
	{ &info_has160, sizeof(has160_ctx), dgshft(has160), iuf(rhash_has160), 0 }, /* 160 bit */
	{ &info_snf128, sizeof(snefru_ctx), dgshft(snefru), ini(rhash_snefru128), upd(rhash_snefru), fin(rhash_snefru), 0 }, /* 128 bit */
	{ &info_snf256, sizeof(snefru_ctx), dgshft(snefru), ini(rhash_snefru256), upd(rhash_snefru), fin(rhash_snefru), 0 }, /* 256 bit */
	{ &info_sha224, sizeof(sha256_ctx), dgshft(sha256), ini(rhash_sha224), upd(rhash_sha256), fin(rhash_sha256), 0 }, /* 224 bit */
	{ &info_sha256, sizeof(sha256_ctx), dgshft(sha256), iuf(rhash_sha256), 0 },  /* 256 bit */
	{ &info_sha384, sizeof(sha512_ctx), dgshft(sha512), ini(rhash_sha384), upd(rhash_sha512), fin(rhash_sha512), 0 }, /* 384 bit */
	{ &info_sha512, sizeof(sha512_ctx), dgshft(sha512), iuf(rhash_sha512), 0 },  /* 512 bit */
	{ &info_edr256, sizeof(edonr_ctx), dgshft2(edonr, u.data256.hash) + 32, iuf(rhash_edonr256), 0 },  /* 256 bit */
	{ &info_edr512, sizeof(edonr_ctx), dgshft2(edonr, u.data512.hash) + 64, iuf(rhash_edonr512), 0 },  /* 512 bit */
	{ &info_sha3_224, sizeof(sha3_ctx), dgshft(sha3), ini(rhash_sha3_224), upd(rhash_sha3), fin(rhash_sha3), 0 }, /* 224 bit */
	{ &info_sha3_256, sizeof(sha3_ctx), dgshft(sha3), ini(rhash_sha3_256), upd(rhash_sha3), fin(rhash_sha3), 0 }, /* 256 bit */
	{ &info_sha3_384, sizeof(sha3_ctx), dgshft(sha3), ini(rhash_sha3_384), upd(rhash_sha3), fin(rhash_sha3), 0 }, /* 384 bit */
	{ &info_sha3_512, sizeof(sha3_ctx), dgshft(sha3), ini(rhash_sha3_512), upd(rhash_sha3), fin(rhash_sha3), 0 }, /* 512 bit */
	{ &info_crc32c, sizeof(uint32_t), 0, iuf(rhash_crc32c), 0 }, /* 32 bit */
};

/**
 * Initialize requested algorithms.
 *
 * @param mask ids of hash sums to initialize
 */
void rhash_init_algorithms(unsigned mask)
{
	(void)mask; /* unused now */

	/* verify that RHASH_HASH_COUNT is the index of the major bit of RHASH_ALL_HASHES */
	assert(1 == (RHASH_ALL_HASHES >> (RHASH_HASH_COUNT - 1)));

#ifdef GENERATE_GOST94_LOOKUP_TABLE
	rhash_gost94_init_table();
#endif
	rhash_uninitialized_algorithms = 0;
}

/**
 * Returns information about a hash function by its hash_id.
 *
 * @param hash_id the id of hash algorithm
 * @return pointer to the rhash_info structure containing the information
 */
const rhash_info* rhash_info_by_id(unsigned hash_id)
{
	hash_id &= RHASH_ALL_HASHES;
	/* check that only one bit is set */
	if (hash_id != (hash_id & -(int)hash_id)) return NULL;
	/* note: alternative condition is (hash_id == 0 || (hash_id & (hash_id - 1)) != 0) */
	return rhash_info_table[rhash_ctz(hash_id)].info;
}

/* CRC32 helper functions */

/**
 * Initialize crc32 hash.
 *
 * @param crc32 pointer to the hash to initialize
 */
static void rhash_crc32_init(uint32_t* crc32)
{
	*crc32 = 0; /* note: context size is sizeof(uint32_t) */
}

/**
 * Calculate message CRC32 hash.
 * Can be called repeatedly with chunks of the message to be hashed.
 *
 * @param crc32 pointer to the hash
 * @param msg message chunk
 * @param size length of the message chunk
 */
static void rhash_crc32_update(uint32_t* crc32, const unsigned char* msg, size_t size)
{
	*crc32 = rhash_get_crc32(*crc32, msg, size);
}

/**
 * Store calculated hash into the given array.
 *
 * @param crc32 pointer to the current hash value
 * @param result calculated hash in binary form
 */
static void rhash_crc32_final(uint32_t* crc32, unsigned char* result)
{
#if defined(CPU_IA32) || defined(CPU_X64)
	/* intel CPUs support assigment with non 32-bit aligned pointers */
	*(unsigned*)result = be2me_32(*crc32);
#else
	/* correct saving BigEndian integer on all archs */
	result[0] = (unsigned char)(*crc32 >> 24), result[1] = (unsigned char)(*crc32 >> 16);
	result[2] = (unsigned char)(*crc32 >> 8), result[3] = (unsigned char)(*crc32);
#endif
}

/**
 * Initialize crc32c hash.
 *
 * @param crc32c pointer to the hash to initialize
 */
static void rhash_crc32c_init(uint32_t* crc32c)
{
	*crc32c = 0; /* note: context size is sizeof(uint32_t) */
}

/**
 * Calculate message CRC32C hash.
 * Can be called repeatedly with chunks of the message to be hashed.
 *
 * @param crc32c pointer to the hash
 * @param msg message chunk
 * @param size length of the message chunk
 */
static void rhash_crc32c_update(uint32_t* crc32c, const unsigned char* msg, size_t size)
{
	*crc32c = rhash_get_crc32c(*crc32c, msg, size);
}

/**
 * Store calculated hash into the given array.
 *
 * @param crc32c pointer to the current hash value
 * @param result calculated hash in binary form
 */
static void rhash_crc32c_final(uint32_t* crc32c, unsigned char* result)
{
#if defined(CPU_IA32) || defined(CPU_X64)
	/* intel CPUs support assigment with non 32-bit aligned pointers */
	*(unsigned*)result = be2me_32(*crc32c);
#else
	/* correct saving BigEndian integer on all archs */
	result[0] = (unsigned char)(*crc32c >> 24), result[1] = (unsigned char)(*crc32c >> 16);
	result[2] = (unsigned char)(*crc32c >> 8), result[3] = (unsigned char)(*crc32c);
#endif
}
