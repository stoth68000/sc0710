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

void sc0710_dma_chains_dump(struct sc0710_dma_channel *ch)
{
	int i;

	printk("%s ch#%d pt_cpu %p  pt_dma %llx  pt_size %d\n",
		ch->dev->name,
		ch->nr,
		ch->pt_cpu, ch->pt_dma, ch->pt_size);

	for (i = 0; i < ch->numDescriptorChains; i++) {
		sc0710_dma_chain_dump(ch, &ch->chains[i], i);
	}
}

void sc0710_dma_chains_free(struct sc0710_dma_channel *ch)
{
	int i;

	/* Free up the SG table */
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
    dma_free_coherent(&((struct pci_dev *)ch->dev->pci)->dev, ch->pt_size, ch->pt_cpu, ch->pt_dma);
    #else
    pci_free_consistent(ch->dev->pci, ch->pt_size, ch->pt_cpu, ch->pt_dma);
    #endif

	for (i = 0; i < ch->numDescriptorChains; i++) {
		sc0710_dma_chain_free(ch, i);
	}
}

int sc0710_dma_chains_alloc(struct sc0710_dma_channel *ch, int total_transfer_size)
{
	int i, ret = 0;

	for (i = 0; i < ch->numDescriptorChains; i++) {
		ret |= sc0710_dma_chain_alloc(ch, i, total_transfer_size);
	}

	return ret;
}

