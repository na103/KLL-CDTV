/*
 * Test of the delta decoder: decodes every frame of a KXL3 file with
 * src/delta.c, checks the block checksums and the frame buffer sums, and
 * writes out the contents of the frame buffer after each frame.
 * tools/kxlcheck.py --raw compares that against the Python decoder. It
 * builds both for the host and for the Amiga (make test also runs it under
 * vamos).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include "delta.h"

static ULONG rd16(const UBYTE *p)
{
	return ((ULONG)p[0] << 8) | p[1];
}

static ULONG rd32(const UBYTE *p)
{
	return (rd16(p) << 16) | rd16(p + 2);
}

int main(int argc, char **argv)
{
	FILE *in, *out;
	UBYTE *data, *fb;
	ULONG size, w, h, planes, block, nblocks, first, plane_size, b, frames = 0, errors = 0;
	UWORD sum = 0;

	if (argc != 3) {
		fprintf(stderr, "usage: %s file.kxl out.raw\n", argv[0]);
		return 2;
	}
	in = fopen(argv[1], "rb");
	if (!in) {
		perror(argv[1]);
		return 1;
	}
	fseek(in, 0, SEEK_END);
	size = ftell(in);
	fseek(in, 0, SEEK_SET);
	data = malloc(size);
	if (!data || size < 32 || fread(data, 1, size, in) != size || memcmp(data, "KXL3", 4)) {
		fprintf(stderr, "%s: not a KXL3 file\n", argv[1]);
		return 1;
	}
	fclose(in);

	w = rd16(data + 4);
	h = rd16(data + 6);
	planes = rd16(data + 8);
	block = rd32(data + 16);
	nblocks = rd32(data + 20);
	first = rd32(data + 28);
	plane_size = (((w + 15) >> 4) << 1) * h;
	if (first + nblocks * block != size) {
		fprintf(stderr, "inconsistent file size\n");
		return 1;
	}
	fb = calloc(plane_size * planes, 1);
	out = fopen(argv[2], "wb");
	if (!fb || !out) {
		perror(argv[2]);
		return 1;
	}

	for (b = 0; b < nblocks; b++) {
		const UBYTE *base = data + first + b * block;
		ULONG n = rd16(base), used = rd16(base + 4), off = 8 + rd16(base + 2), i;
		UWORD cks = 0;

		for (i = 8; i + 1 < used; i += 2)
			cks += rd16(base + i);
		if (cks != rd16(base + 6)) {
			fprintf(stderr, "block %lu: wrong checksum\n", (unsigned long)b);
			errors++;
		}
		for (i = 0; i < n; i++) {
			ULONG vsize = rd16(base + off);
			UWORD fbsum = rd16(base + off + 2);

			off += 4;
			if (off + vsize > used) {
				fprintf(stderr, "block %lu: frame past the end\n", (unsigned long)b);
				return 1;
			}
			if (!delta_decode(base + off, fb, plane_size, planes, &sum)) {
				fprintf(stderr, "frame %lu: copy past the end of the bitplane\n", (unsigned long)frames);
				errors++;
			}
			if (sum != fbsum) {
				fprintf(stderr, "frame %lu: wrong frame buffer sum\n", (unsigned long)frames);
				errors++;
				sum = fbsum;
			}
			fwrite(fb, 1, plane_size * planes, out);
			off += vsize;
			frames++;
		}
	}
	fclose(out);
	printf("%lu frames decoded, %lu errors\n", (unsigned long)frames, (unsigned long)errors);
	return errors ? 1 : 0;
}
