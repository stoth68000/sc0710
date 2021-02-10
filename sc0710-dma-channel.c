/*
 *  Driver for the Elgato 4k60 Pro mk.2 HDMI capture card.
 *
 *  Copyright (c) 2021 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
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

/* We're not doing IRQ and interrupt servicing of the dma subsystem, instead
 * we're going to rely on a 2ms kernel thread to poll and dequeue
 * buffers.
 *
 * This matches the windows driver design, and after review the industry
 * (xilinx) believe that a looping descriptor set that needs no servicing
 * keeps the DMA buss 100% busy, all the time and maximises throughput on
 * the DMA channel. Where as, waiting for an
 * interrupt to services the DMA system (which its stopped) introduces
 * unwanted latency.
 *
 * The basic design for this driver is, every 2ms this function is called.
 * We'll read the DMA controller count register for this channel,
 * if its changed since the last time we read - then another descriptor
 * has completed. Upon descriptor completion, we'll look up which descriptor
 * has changed, and copy the data out of the descriptor buffer BEFORE
 * the dma subsystem has chance to ovewrite it.
 * Each channel has N descriptors, a minimum of four. So, out latency is
 * the counter changes, we notice 2ms later, we spend micro seconds
 * looking at each descriptor in turn (N - typically 6), when we detect
 * that its changed, we'll immediately memcpy the dma dest buffer
 * into a previously allocated user facing buffer.
 *
 * 1. We'll allocate two PAGE of PCIe root addressible ram
 *    to hold a) descriptors and b) metadata writeback data provided
 *    by the card root controller.
 *
 *    0x0000  descriptor1
 *    0x0020  descriptor2
 *    0x0040  descriptor3
 *    0x0060  descriptor4
 *    0x0080  descriptor5
 *    0x00a0  descriptor6
 *    0x1000  descriptor1 writeback metadata location
 *    0x1020  descriptor2 writeback metadata location
 *    0x1030  descriptor3 writeback metadata location
 *    0x1040  descriptor4 writeback metadata location
 *    0x1050  descriptor5 writeback metadata location
 *    0x1060  descriptor6 writeback metadata location
 *
 * 2. The descriptors will contain lengths for the dma transfer and
 *    locations for the metadata writeback to happen.
 *
 * 3. We'll allocate a single large DMA addressible buffer
 *    to hold the final pixels and audio. These will be references
 *    by the descriptors
 *
 *    For each descriptor (6 of) we allocate a large buffer to contain the entire image
 *
 *    picbuf1 1fa400 length
 *    picbuf2 1fa400 length
 *    picbuf3 1fa400 length
 *    picbuf4 1fa400 length
 *    picbuf5 1fa400 length
 *    picbuf6 1fa400 length
 *
 *    Update each descriptor to point to the correct picture buffer and provide
 *    a dma transfer length.
 *
 *    0x0000  descriptor1  len, dma address of picbuf1, addr of descriptor2
 *    0x0020  descriptor2  len, dma address of picbuf2, addr of descriptor3
 *    0x0040  descriptor3  len, dma address of picbuf3, addr of descriptor4
 *    0x0060  descriptor4  len, dma address of picbuf4, addr of descriptor5
 *    0x0080  descriptor5  len, dma address of picbuf5, addr of descriptor6
 *    0x00a0  descriptor6  len, dma address of picbuf6, addr of descriptor1
 *    0x1000  descriptor1 writeback metadata location
 *    0x1020  descriptor2 writeback metadata location
 *    0x1030  descriptor3 writeback metadata location
 *    0x1040  descriptor4 writeback metadata location
 *    0x1050  descriptor5 writeback metadata location
 *    0x1060  descriptor6 writeback metadata location
 *
 * Return < 0 on error
 * Return number of buffers we copyinto from dma into user buffers.
 */
int sc0710_dma_channel_service(struct sc0710_dma_channel *ch)
{
	struct sc0710_dma_descriptor *desc = (struct sc0710_dma_descriptor *)ch->pt_cpu;
	int i, processed = 0;
	u32 v, ctrl;
	u32 wbm[2];
	u32 *p = (u32 *)ch->pt_cpu;
	u32 q;
	int dequeueItem = 0;

	p += (PAGE_SIZE / 4);

	if (ch->enabled == 0)
		return -1;

	v = sc_read(ch->dev, 1, ch->reg_dma_completed_descriptor_count);
	if (v == ch->dma_completed_descriptor_count_last) {
		/* No new buffers since our last service call. */
		return processed;
	}

	printk("ch#%d    was %d now %d\n", ch->nr, ch->dma_completed_descriptor_count_last, v);

	ch->dma_completed_descriptor_count_last = v;

	/* Check all the descriptors and see which on finished. */
	for (i = 0; i < ch->numDescriptors; i++) {
		dequeueItem = 0;
		ctrl = desc->control;

		q = (PAGE_SIZE + (i * (0x20))) / sizeof(u32);

		wbm[0] = *(p + 0);
		wbm[1] = *(p + 1);

		if (wbm[0] || wbm[1]) {
			dequeueItem = 1;
		}

#if 0
		printk("%s ch#%d    [%02d] %08x - wbm %08x %08x   q: %08x  p: %p%s\n",
			ch->dev->name,
			ch->nr,
			i,
			desc->control,
			wbm[0],
			wbm[1], q, p, dequeueItem ? " (DQ)" : "");
#endif

		if (dequeueItem)
		{
			/* TODO: dequeue this descriptor picbuf */
			printk("%s ch#%d    [%02d] %08x - wbm %08x %08x   q: %08x  p: %p%s\n",
				ch->dev->name,
				ch->nr,
				i,
				desc->control,
				wbm[0],
				wbm[1], q, p, dequeueItem ? " (DQ)" : "");

			sc0710_things_per_second_update(&ch->bitsPerSecond, wbm[1] * 8);
			sc0710_things_per_second_update(&ch->descPerSecond, 1);

			/* Reset the metadat so we don't attempt to process this during the
 			 * next service call.
 			 */
			*(p + 0) = 0;
			*(p + 1) = 0;
		}
		p += 8;

		desc++;
	}

	return processed;
}

void sc0710_dma_channel_descriptors_dump(struct sc0710_dma_channel *ch)
{
	int i;
	struct sc0710_dma_descriptor *desc = (struct sc0710_dma_descriptor *)ch->pt_cpu;

	printk("%s  pt_cpu %p  pt_dma %llx  pt_size %d\n",
		ch->dev->name,
		ch->pt_cpu, ch->pt_dma, ch->pt_size);

	/* Create a set of linked scatter gather descriptors and allocate
	 * supporting dma buffers for the PCIe endpoint to burst into.
	 */
	for (i = 0; i < ch->numDescriptors; i++) {
		printk("%s buf_cpu %p buf_dma %llx buf_size %d\n",
			ch->dev->name,
			ch->buf_cpu[i], ch->buf_dma[i], ch->buf_size);
	}
	for (i = 0; i < ch->numDescriptors; i++) {
		printk("%s         [%02d] %08x %08x %08x %08x %08x %08x %08x %08x\n",
			ch->dev->name,
			i,
			desc->control,
			desc->lengthBytes,
			desc->src_l,
			desc->src_h,
			desc->dst_l,
			desc->dst_h,
			desc->next_l,
			desc->next_h);
		desc++;
	}
}

static void sc0710_dma_channel_descriptors_free(struct sc0710_dma_channel *ch)
{
	int i;

	pci_free_consistent(ch->dev->pci, ch->pt_size, ch->pt_cpu, ch->pt_dma);

	for (i = 0; i < ch->numDescriptors; i++) {
		pci_free_consistent(ch->dev->pci, ch->buf_size, ch->buf_cpu[i], ch->buf_dma[i]);
	}
}

static int sc0710_dma_channel_descriptors_alloc(struct sc0710_dma_channel *ch)
{
	struct sc0710_dma_descriptor *desc;
	int i;

	/* allocate the descriptor table ram, its contigious. */
	ch->pt_cpu = pci_alloc_consistent(ch->dev->pci, ch->pt_size, &ch->pt_dma);
	if (ch->pt_cpu == 0)
		return -1;

	memset(ch->pt_cpu, 0, ch->pt_size);

	desc = (struct sc0710_dma_descriptor *)ch->pt_cpu;

	/* Create a set of linked scatter gather descriptors and allocate
	 * supporting dma buffers for the PCIe endpoint to burst into.
	 */
	for (i = 0; i < ch->numDescriptors; i++) {
		ch->buf_cpu[i] = pci_alloc_consistent(ch->dev->pci, ch->buf_size, &ch->buf_dma[i]);
		if (!ch->buf_cpu[i]) {
			return -1;
		}

		desc->control = 0xAD4B0000;
		//desc->control |= (1 << 1); /* Enabled 'Completed' bit */
		desc->lengthBytes = ch->buf_size;
		desc->src_l = (u64)ch->pt_dma + PAGE_SIZE + (i * 0x20);
		desc->src_h = ((u64)ch->pt_dma + PAGE_SIZE + (i * 0x20)) >> 32;
		desc->dst_l = (u64)ch->buf_dma[i];
		desc->dst_h = (u64)ch->buf_dma[i] >> 32;

		if (i + 1 == ch->numDescriptors) {
			/* Last descriptor needs to point to the first. */
			desc->next_l = (u64)ch->pt_dma;
			desc->next_h = (u64)ch->pt_dma >> 32;
		} else {
			/* other descriptors point to the next descriptor in the chain. */
			desc->next_l = (u64)ch->pt_dma + ((i + 1) * sizeof(*desc));
			desc->next_h = ((u64)ch->pt_dma + ((i + 1) * sizeof(*desc))) >> 32;
		}

		desc++;
	}

	return 0;
}

int sc0710_dma_channel_alloc(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction,
	u32 baseaddr,
	enum sc0710_channel_type_e mediatype)
{
	int ret;
	struct sc0710_dma_channel *ch = &dev->channel[nr];
	if (nr >= SC0710_MAX_CHANNELS)
		return -1;

	if (direction != CHDIR_INPUT)
		return -1;

	memset(ch, 0, sizeof(*ch));
	mutex_init(&ch->lock);

	ch->dev = dev;
	ch->nr = nr;
	ch->enabled = 1;
	ch->direction = direction;
	ch->mediatype = mediatype;
	sc0710_things_per_second_reset(&ch->bitsPerSecond);
	sc0710_things_per_second_reset(&ch->descPerSecond);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ch->numDescriptors = 6;
		ch->buf_size = 0x1fa400;
	} else
	if (ch->mediatype == CHTYPE_AUDIO) {
		ch->numDescriptors = 4;
		ch->buf_size = 0x4000;
	} else {
		ch->numDescriptors = 0;
	}

	/* Page table defaults. */
	/* This assumed PAGE_SIZE is 4K */
	ch->pt_size = PAGE_SIZE * 2;

	/* register offsets use by the channel and dma descriptor register writes/reads. */

	/* DMA controller */
	ch->register_dma_base = baseaddr;
	ch->reg_dma_control = ch->register_dma_base + 0x04;
	ch->reg_dma_control_w1s = ch->register_dma_base + 0x08;
	ch->reg_dma_control_w1c = ch->register_dma_base + 0x0c;
	ch->reg_dma_status1 = ch->register_dma_base + 0x40;
	ch->reg_dma_status2 = ch->register_dma_base + 0x44;
	ch->reg_dma_completed_descriptor_count = ch->register_dma_base + 0x48;
	ch->reg_dma_poll_wba_l = ch->register_dma_base + 0x88;
	ch->reg_dma_poll_wba_h = ch->register_dma_base + 0x8c;

	/* SGDMA Controller */
	ch->register_sg_base = baseaddr + 0x4000;
        ch->reg_sg_start_l = ch->register_sg_base + 0x80;
        ch->reg_sg_start_h = ch->register_sg_base + 0x84;
        ch->reg_sg_adj = ch->register_sg_base + 0x88;
        ch->reg_sg_credits = ch->register_sg_base + 0x8c;

	sc0710_dma_channel_descriptors_alloc(ch);

	printk(KERN_INFO "%s channel %d allocated\n", dev->name, nr);

	sc0710_dma_channel_descriptors_dump(ch);

	if (ch->mediatype == CHTYPE_VIDEO) {
		//ret = sc0710_video_register(ch);
	}

	return 0; /* Success */
};

void sc0710_dma_channel_free(struct sc0710_dev *dev, u32 nr)
{
	struct sc0710_dma_channel *ch = &dev->channel[nr];
	if (nr >= SC0710_MAX_CHANNELS)
		return;

	if (ch->enabled == 0)
		return;

	ch->enabled = 0;

	if (ch->mediatype == CHTYPE_VIDEO) {
		//sc0710_video_unregister(ch);
	}

	sc0710_dma_channel_descriptors_free(ch);

	printk(KERN_INFO "%s channel %d deallocated\n", dev->name, nr);
}

int sc0710_dma_channel_start_prep(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1c, 0x00000001);

	ch->dma_completed_descriptor_count_last = 0;
	sc_write(ch->dev, 1, ch->reg_dma_completed_descriptor_count, 1);
	sc_write(ch->dev, 1, ch->reg_sg_start_h, ch->pt_dma >> 32);
	sc_write(ch->dev, 1, ch->reg_sg_start_l, ch->pt_dma);
	sc_write(ch->dev, 1, ch->reg_sg_adj, 0);

	return 0;
}

int sc0710_dma_channel_stop(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1c, 0x00000001);
	sc0710_things_per_second_reset(&ch->bitsPerSecond);
	sc0710_things_per_second_reset(&ch->descPerSecond);
	return 0;
}

int sc0710_dma_channel_start(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1s, 0x00000001);
	return 0;
}

