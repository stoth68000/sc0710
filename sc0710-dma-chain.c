/*
 *  Driver for the Elgato 4k60 Pro mk.2 HDMI capture card.
 *
 *  Copyright (c) 2021-2022 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Each FPGA descriptor is 8xDWORD.
 * We're going to have 8 descriptors per channel, where
 * each descriptor is either a frame of video or a 'chunk' (TBD)
 * of audio.
 * The entire descriptor pagetable for a single channel will fit
 * inside a single page of memory.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include "sc0710.h"

static int dma_chain_debug = 2;
#define dprintk(level, fmt, arg...)\
        do { if (dma_chain_debug >= level)\
                printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
        } while (0)

/* Copy the chain contents into a target buffer, don't overflow.
 * Return numbers of bytes, or < 0 if overflow detected.
 */
int sc0710_dma_chain_dq_to_ptr(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain, u8 *dst, int dstlen)
{
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	int len = 0;
	int i;

	for (i = 0; i < chain->numAllocations; i++) {
		if (len + dca->buf_size <= dstlen) {
			memcpy(dst + len, dca->buf_cpu, dca->buf_size);
			len += dca->buf_size;
		} else {
			return -EOVERFLOW;
		}
		dca++;
	}

	return len;
}

void sc0710_dma_chain_dump(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain, int nr)
{
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	struct sc0710_dma_descriptor *desc;
	int i;

	printk("               chain[%02d]  %p -- enabled %d total_transfer_size 0x%x numAllocations %d\n",
		nr,
		chain, chain->enabled, chain->total_transfer_size, chain->numAllocations);

	for (i = 0; i < chain->numAllocations; i++) {
		desc = dca->desc;
		printk("                          [%02d] enabled %d buf_size 0x%x buf_dma %llx buf_cpu %p  wbm: %p / %p\n",
			i,
			dca->enabled,
			dca->buf_size,
			dca->buf_dma,
			dca->buf_cpu,
			dca->wbm[0],
			dca->wbm[1]);
		printk("                               %08x %08x %08x %08x %08x %08x %08x %08x\n",
			desc->control,
			desc->lengthBytes,
			desc->src_l,
			desc->src_h,
			desc->dst_l,
			desc->dst_h,
			desc->next_l,
			desc->next_h);
		dca++;
	}
}

void sc0710_dma_chain_free(struct sc0710_dma_channel *ch, int nr)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_dma_descriptor_chain *chain = &ch->chains[nr];
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	int i;

	dprintk(1, "%s(ch#%d)\n", __func__, nr);

	chain->enabled = 0;

	for (i = 0; i < chain->numAllocations; i++) {
		pci_free_consistent(dev->pci, dca->buf_size, dca->buf_cpu, dca->buf_dma);
		dca++;
	}
}

int sc0710_dma_chain_alloc(struct sc0710_dma_channel *ch, int nr, int total_transfer_size)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_dma_descriptor_chain *chain = &ch->chains[nr];
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	int rem = total_transfer_size;
	int size;
	int segsize = 4 * 1048576;

	chain->enabled = 1;
	chain->total_transfer_size = total_transfer_size;
	chain->numAllocations = 0;

	/* First thing we should do is determine all of the allocations
	 * for the total transfer_size, build the segment sizes and alloc
	 * in the PCI DMA space.
	 */
	while (rem > 0) {
		/* Determine the size of this dma allocation segment. */
		if (rem > segsize)
			size = segsize;
		else
			size = rem;

		dca->enabled = 1;
		dca->buf_size = size;
		dca->buf_cpu = pci_alloc_consistent(dev->pci, dca->buf_size, &dca->buf_dma);
		if (dca->buf_cpu == 0)
			return -1;

		memset(dca->buf_cpu, 0, dca->buf_size);

		if (++chain->numAllocations == SC0710_MAX_CHAIN_DESCRIPTORS) {
			/* We can't fit the transfer in our statically allocated structs. */
			return -1;
		}
		dca++;
		rem -= size;
	}
	return 0; /* Success */
}
