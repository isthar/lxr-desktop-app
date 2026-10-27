/*
 * test_dmabuf.c
 *
 * Host test of the codec's DMA buffers (stage 1b F7): dma_buffer and
 * dma_buffer2 in Core/Src/Soundengine/AudioCodecManager.c are int16_t arrays,
 * and the mixer writes them as int32_t. On the device they sit at 4-byte
 * aligned addresses only because GCC places arrays so; their declaration asked
 * for 2. A host compiler may place them at any even address, and UBSan's
 * alignment check in the emulator stops on such a store. The definition now
 * asks for 4 bytes.
 *
 * The alignment of a definition is visible only in its own translation unit,
 * so this file includes AudioCodecManager.c itself. It casts a pointer to
 * uint32_t, which a 64-bit host warns about.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "seqc_test.h"

#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#include "../Core/Src/Soundengine/AudioCodecManager.c"
#pragma GCC diagnostic pop

/* CodecInit() hands the buffers to the codec driver (cs4344_cs5343.c) */
void codec_initCsCodec(uint32_t Addr1, uint32_t Size1, uint32_t Addr2, uint32_t Size2)
{
	(void)Addr1; (void)Size1; (void)Addr2; (void)Size2;
}

TEST(dmabuf_buffers_are_4_byte_aligned)
{
	ASSERT_TRUE(__alignof__(dma_buffer) >= 4);
	ASSERT_TRUE(__alignof__(dma_buffer2) >= 4);
	ASSERT_EQ((uintptr_t)dma_buffer % 4, 0);
	ASSERT_EQ((uintptr_t)dma_buffer2 % 4, 0);
}

TEST_MAIN()
