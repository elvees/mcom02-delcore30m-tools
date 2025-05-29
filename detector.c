/*
 * \file
 * \brief detector - Motion detector
 * on Elcore-30M
 *
 * \copyright
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define PADDR(a) (a&0x7fffffff)

#define DMA_REG_INTMIS 0x37220028
#define DMA_REG_INTCLR 0x3722002C
#define DMA_REG_DBGSTATUS 0x37220D00
#define DMA_REG_DBGCMD 0x37220D04
#define DMA_REG_DBGINST0 0x37220D08
#define DMA_REG_DBGINST1 0x37220D0C

/* FIXME: It is using instead of global variable */
#define DMA_READY_REG 0x3A43FFF0

#define HW_SPINLOCK 0x38081804

struct tileinfo {
	uint32_t x, y;
	uint32_t width, height;
	uint32_t stride[2];
};

struct tilesbuffer {
	uint32_t ntiles;
	uint32_t tilesize;
	uint32_t pixel_size;
	struct tileinfo info[];
};

struct dsp_struct_data {
	uint32_t channels[8];
	uint32_t avering_counter;
	uint32_t flag_avered;
};

/*
* Function: int readFromExtMem(int addr)
* Description: loads value from external memory.
* Input: addr - address of external memory, must be aligned at 4
* Output: int - loaded value
*/
volatile uint32_t readFromExtMem(uint32_t addr)
{
	int val = 0;
	addr = PADDR(addr);
	asm volatile("lsrl 2, %0, r6.l"::"r"(addr));
	asm volatile("move r6.l, a5.l":::"a5.l");
	asm volatile("move (a5.l), r6.l");
	asm volatile("move r6.l, %0":"=r"(val));
	return val;
}

/*
* Function: void writeToExtMem(int addr, int value)
* Description: loads value into external memory.
* Input: addr - address of external memory, must be aligned at 4
* value - value to store
*/
volatile void writeToExtMem(uint32_t addr, uint32_t value)
{
	addr = PADDR(addr);
	asm volatile("lsrl 2, %0, r6.l"::"r"(addr));
	asm volatile("move r6.l, a5.l":::"a5.l");
	asm volatile("move %0, (a5.l)"::"r"(value));
}

/* FIXME: It is using instead of global variable */
uint32_t get_dma_channel_busy_reg()
{
	uint32_t retval;
	asm volatile(
		"move %1, R6.l\n\t"
		"asrl 2, R6.l, R6.l\n\t"
		"move R6.s, A0\n\t"
		"move (A0.l), R6.l\n\t"
		"move R6.l, %0\n\t"
		:"=r"(retval):"r"(DMA_READY_REG)
	);
	return retval;
}

/* FIXME: It is using instead of global variable */
void set_dma_channel_busy_reg(uint32_t val)
{
	asm volatile(
		"move %0, R6.l\n\t"
		"asrl 2, R6.l, R6.l\n\t"
		"move R6.s, A0\n\t"
		"move %1, R6.l\n\t"
		"move R6.l, (A0.l)\n\t"
		::"r"(DMA_READY_REG),"r"(val)
	);
}

void interrupt_handler()
{
	/* get interrupts number */
	uint32_t reg_val = readFromExtMem(DMA_REG_INTMIS);

	/* reset interrupts */
	writeToExtMem(DMA_REG_INTCLR, reg_val);

	/* DMA channels competed. Reset bit in dma_channel_busy_reg */
	uint32_t dma_channel_busy_reg = get_dma_channel_busy_reg();
	dma_channel_busy_reg &= ~reg_val;
	set_dma_channel_busy_reg(dma_channel_busy_reg);
}

void start_dma_channel(uint32_t dma_channel)
{
	uint32_t old_imaskr;

	/* disable irqs */
	asm volatile(
		"move IMASKR, %0\n\t"
		"clrl R6.l\n\t"
		"move R6.l, IMASKR\n\t"
		:"=r"(old_imaskr)
	);

	/* DMA channel busy. Set bit in dma_channel_busy_reg */
	uint32_t dma_channel_busy_reg = get_dma_channel_busy_reg();
	dma_channel_busy_reg |= (1 << dma_channel);
	set_dma_channel_busy_reg(dma_channel_busy_reg);

	/* Getting code of instruction *send event to dma_channel* */
	uint32_t regval = ((dma_channel + 8) << 27) + 0x340000;

	/* lock HW spinlock by reading 0 value */
	while (readFromExtMem(HW_SPINLOCK) & 1)
		continue;

	/* check wait while dbgstatus != 0 */
	while (readFromExtMem(DMA_REG_DBGSTATUS) & 1)
		continue;

	/* execute instruction *send event to dma_channel* */
	writeToExtMem(DMA_REG_DBGINST0, regval);
	writeToExtMem(DMA_REG_DBGINST1, 0);
	writeToExtMem(DMA_REG_DBGCMD, 0);

	/* unlock HW spinlock */
	writeToExtMem(HW_SPINLOCK, 0);

	/* enable irqs */
	asm volatile("move %0, IMASKR"::"r"(old_imaskr));
}
void dsp_memcpy(uint32_t *src, uint32_t *dst, size_t pixels)
{
	asm volatile (
		"move %0, R6.l\n\t"
		"asrl 2, R6.l, R6.l\n\t"
		"move R6.s, A3\n\t"
		"move %1, R6.l\n\t"
		"asrl 2, R6.l, R6.l\n\t"
		"move R6.s, A4\n\t"
		"move %2, R6.l\n\t"
	"start_copy:\n\t"
		"move (A3.l)+, R16.q\n\t"
		"move R16.q, (A4.l)+\n\t"
		"subl 4, R6.l\n\t"
		"cmpl 0, R6.l\n\t"
		"b.ne start_copy\n\t"
		::"r"(src), "r"(dst), "r"(pixels)
	);
}

void dsp_clear_memory(uint32_t *src, size_t pixels)
{
	asm volatile (
		"move %0, R6.l\n\t"
		"asrl 2, R6.l, R6.l\n\t"
		"move R6.s, A3\n\t"
		"move %1, R26.l\n\t"
		"lsrl 4, R26.l\n\t"
		"sb16 r6.q, r6.q, r6.q\n\t"
		"do r26.s, clear_memory_end\n\t"
	"clear_memory_end:\n\t"
		"move r6.q, (A3.l)+\n\t"
		::"r"(src), "r"(pixels)
	);
}

void detector(uint32_t *src, size_t pixels, struct dsp_struct_data* data,
	      uint32_t *background)
{
	asm volatile(
		"lsrl 2, %0, r0.l\n\t"
		"move r0.s, a1     ;isrc\n\t"
		"move r0.s, a2     ;idst\n\t"
		"lsrl 2, %1, r0.l\n\t"
		"move r0.s, at     ;background\n\t"
		"move %2,r14.l          ;nPixels\n\t"
		"move 0x3F3F3F,r2.l    ;PixelTh\n\t"

		"move ccr,r24.s\n\t"
		"move pdnr,r25.s\n\t"
		"andl 0x7CFFFEFF,r20.l,r6.l\n\t"
		"orl  0x81000100,r6.l,r6.l    ;Set saturation mode, and scale=1\n\t"
		"move r6.s,ccr\n\t"

		"move 4,r8.l\n\t"
		"move r8.s,it.s    ;Increment for Fone\n\t"

		"move a1.l,r8.l\n\t"
		"move r8.l,a0.l  ;Second Ptr to iSrc\n\t"

		"orl 0xFF000000,r2.l ;PixelTh\n\t"
		"move r2.l,r12.l\n\t"
		"move r2.l,r13.l\n\t"
		"move r12.d,r13.d\n\t"

		"lsrl 2, r14.l,r14.l   ;nPixels/4\n\t"
		"move 31,r15.s       ;shift for aslxl\n\t"

		"move 0x00800000,r16.l     ;Rmask\n\t"
		"move r16.l,r17.l\n\t"
		"move r16.d,r17.d\n\t"

		"move (a0)+,r6.q         move (at)+it,r0.q\n\t"

		"umfb16 r16.q,r6.q,r6.q  msb16 r6.q,r0.q,r0.q\n\t"
		"                        sb16 r12,r0.q,r2.q\n\t"
		"sb16 r6.q,r16.q,r8.q    aslxl r15,r2.d,r2.d     move (a0)+,r6.q         move (at)+it,r0.q\n\t"
		"move (a1)+,r10.q        aslxl r15,r3.d,r3.d\n\t"
		"umfb16 r16.q,r6.q,r6.q  msb16 r6.q,r0.q,r0.q\n\t"
		"umfb16 r8.q,r2.q,r4.q   sb16 r12,r0.q,r2.q\n\t"
	"do r14.s,mode_det\n\t"
		"sb16 r6.q,r16.q,r8.q    aslxl r15,r2.d,r2.d     move (a0)+,r6.q         move (at)+it,r0.q\n\t"
		"ab16 r10.q,r4.q,r4.q    aslxl r15,r3.d,r3.d     move (a1)+,r10.q\n\t"
		"umfb16 r16.q,r6.q,r6.q  msb16 r6.q,r0.q,r0.q\n\t"
	"mode_det:\n\t"
		"umfb16 r8.q,r2.q,r4.q   sb16 r12,r0.q,r2.q      move r4.q,(a2)+\n\t"

		"move r24.s,ccr\n\t"
		::"r"(src), "r"(background), "r"(pixels)
	);
}

/*
 * FIXME: If used functon for waiting dma_channels with loop
 * while ((* (uint32_t *) 0x3A43FFF0) & (1 << channel)) - no loop hanging
 * occured.But if use while (get_dma_channel_busy_reg() & (1 << channel)) -
 * loop hanging occures
 */
int start(uint32_t thread_num, uint32_t unused0, uint32_t unused1,
	  uint32_t *src_img, uint32_t *tile_buf1,
	  struct dsp_struct_data *dsp_struct_data, uint32_t *tile_buf2,
	  uint32_t *unused2, uint32_t *unused3, struct tilesbuffer *tileinfo,
	  uint32_t *background_tile1, uint32_t *background_tile2)
{
	uint32_t dma_channels[4] = {
		dsp_struct_data->channels[0],
		dsp_struct_data->channels[1],
		dsp_struct_data->channels[2],
		dsp_struct_data->channels[3]
	};

	uint32_t dma_channels_mask[4];

	for (int i = 0; i < 4; ++i)
		dma_channels_mask[i] = 1 << dma_channels[i];

	uint32_t *tile_buffers[] = {tile_buf1, tile_buf2};
	uint32_t *background_buffers[] = {background_tile1, background_tile2};

	set_dma_channel_busy_reg(0);

	uint32_t tile_odd = 0;
	uint32_t stride = tileinfo->info[0].stride[0];

	uint32_t *dma_channel_busy_reg = (uint32_t *) DMA_READY_REG;

	/*
	 * FIXME: Can't to run overlap input and output channels
	 * FIXME: Doesn't work with clang keys -O1, -O2, -O3
	 */
	for (size_t i = 0, tile_odd = 0; i < tileinfo->ntiles; ++i, tile_odd ^= 1) {
		start_dma_channel(dma_channels[0]);
		start_dma_channel(dma_channels[2]);

		size_t size = tileinfo->info[i].height *
			      tileinfo->info[i].width *
			      tileinfo->info[i].stride[0];
		size /= stride;
		while (*dma_channel_busy_reg &
			(dma_channels_mask[0] | dma_channels_mask[2]));

		if(!dsp_struct_data->flag_avered && dsp_struct_data->avering_counter >= 30)
			dsp_memcpy(tile_buffers[tile_odd],
				   background_buffers[tile_odd], size);
		else
			detector(tile_buffers[tile_odd], size, dsp_struct_data,
				 background_buffers[tile_odd]);

		/*
		 * FIXME: Can't to run in parallel 2 output DMA-channels
		 */
		start_dma_channel(dma_channels[1]);
		while (*dma_channel_busy_reg & dma_channels_mask[1]);
		start_dma_channel(dma_channels[3]);
		while (*dma_channel_busy_reg & dma_channels_mask[3]);
	}

	if (dsp_struct_data->avering_counter >= 30)
		dsp_struct_data->flag_avered = 1;
	else
		dsp_struct_data->avering_counter += 1;
	return 0;
}
