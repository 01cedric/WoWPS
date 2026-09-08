#ifndef OPENGNM_COMPAT_GNF_H
#define OPENGNM_COMPAT_GNF_H

/*
 * Minimal GNF (GNM Texture File) format types and helpers for
 * downstream consumers that need to parse GNF texture files
 * (e.g. freegnm-examples shared code).
 * Full GNF validation tools remain in the separate opengnm-tools repo.
 */

#include <stdint.h>
#include <gnm_texture.h>

#define GNF_HEADER_MAGIC 0x20464e47  /* "GNF " */
#define GNF_USER_MAGIC 0x52455355    /* "USER" */

typedef struct {
	uint32_t magic;
	uint32_t contentssize;
} GnfHeader;
_Static_assert(sizeof(GnfHeader) == 0x8, "");

typedef struct {
	uint8_t version;
	uint8_t numtextures;
	uint8_t alignment;
	uint8_t _unused;

	uint32_t streamsize;
	GnmTexture textures[];
} GnfContents;
_Static_assert(sizeof(GnfContents) == 0x8, "");

typedef struct {
	uint32_t magic;
	uint32_t datasize;
	uint8_t data[];
} GnfUserData;
_Static_assert(sizeof(GnfUserData) == 0x8, "");

static inline uint32_t gnfGetTextureOffset(
    const GnfHeader* header, uint32_t texidx
) {
	if (!header) {
		return 0;
	}

	const GnfContents* contents =
	    (const GnfContents*)((const uint8_t*)header + sizeof(GnfHeader));
	if (texidx >= contents->numtextures) {
		return 0;
	}

	const GnmTexture* tex = &contents->textures[texidx];
	const uint32_t arraystart = sizeof(GnfHeader) + header->contentssize;
	const uint32_t offset = ((const uint32_t*)tex)[0];
	return arraystart + offset;
}

static inline uint32_t gnfGetTextureSize(
    const GnfHeader* header, uint32_t texidx
) {
	if (!header) {
		return 0;
	}

	const GnfContents* contents =
	    (const GnfContents*)((const uint8_t*)header + sizeof(GnfHeader));
	if (texidx >= contents->numtextures) {
		return 0;
	}

	const GnmTexture* tex = &contents->textures[texidx];
	return ((const uint32_t*)tex)[7];
}

static inline uint32_t gnfGetTextureAlignment(
    const GnfHeader* header, uint32_t texidx
) {
	if (!header) {
		return 0;
	}

	const GnfContents* contents =
	    (const GnfContents*)((const uint8_t*)header + sizeof(GnfHeader));
	if (texidx >= contents->numtextures) {
		return 0;
	}

	const GnmTexture* tex = &contents->textures[texidx];
	return 1 << (((const uint32_t*)tex)[1] & 0xff);
}

#endif /* OPENGNM_COMPAT_GNF_H */
