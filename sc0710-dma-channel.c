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

static int dma_channel_debug = 1;
#define dprintk(level, fmt, arg...)\
        do { if (dma_channel_debug >= level)\
                printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
        } while (0)

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
 * We'll read the DMA controller 'descriptors count complete' register
 * for this channel.
 *
 * If the descriptor counter has changed since the last time we read
 * then another descriptor has completed.
 *
 * Upon descriptor completion, we'll look up which descriptor
 * has changed, and copy the data out of the descriptor buffer BEFORE
 * the dma subsystem has chance to ovewrite it.
 *
 * Each channel has N chains of descriptors, a minimum of four.
 * So, our latency is  the counter changes, we notice 2ms later, we spend micro seconds
 * looking at each descriptor in turn (N - typically 6), when we detect
 * that its changed, we'll immediately memcpy the dma dest buffer
 * into a previously allocated user facing video 4 linux buffer.
 *
 * 1. We'll allocate two PAGE of PCIe root addressible ram
 *    to hold a) scatter gather descriptors and
 *            b) metadata writeback data provided
 *    by the card root controller.
 *
 *    When the DMA controller finished a descriptor, it updates
 *    the metadata writeback so we'll monitor the metadata to see
 *    which descriptor chains have finished.
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
 * 3. We'll allocate multiple large DMA addressible buffers
 *    to hold the final pixels and audio. These will be references
 *    by the descriptors in each chain. A chain is expected to
 *    contain a single video picture, and create multiple
 *    buffers and point multiple descriptors at the buffers
 *    in order that the DMA controller can DMA the data into RAM.
 *
 * Return < 0 on error
 * Return number of buffers we copyinto from dma into user buffers.
 */
static void sc0710_dma_dequeue_video(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_buffer *vb_buf = NULL;
	unsigned long flags;
	u8 *dst = NULL;
	int len;

	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);

	do
	{
		if (list_empty(&ch->v4l2_capture_list))
			break;

		vb_buf = list_entry(ch->v4l2_capture_list.next, struct sc0710_buffer, vb.queue);
		if (vb_buf->vb.state != VIDEOBUF_QUEUED) {
			printk(KERN_ERR "%s() vb was not QUEUED, is 0x%x\n", __func__, vb_buf->vb.state);
			break;
		}

		dst = videobuf_to_vmalloc(&vb_buf->vb);
		if (!dst) {
			printk(KERN_ERR "%s() vb not accessible\n", __func__);
			break;
		}

		/* Copy dma data to user buffer. */
		dprintk(3, "%s() copying %lu bytes\n", __func__, vb_buf->vb.size);

		len = sc0710_dma_chain_dq_to_ptr(ch, chain, dst, vb_buf->vb.size);
		if (len != vb_buf->vb.size) {
			printk("%s() error copying %lu bytes, copied %d\n", __func__, vb_buf->vb.size, len);
		}

		do_gettimeofday(&vb_buf->vb.ts);
		list_del(&vb_buf->vb.queue);
#if 0
        if (do_colorbars && (errors_on_colorsbars == 1))
            vb_buf->vb.state = VIDEOBUF_ERROR;
        else
#endif
		vb_buf->vb.state = VIDEOBUF_DONE;
		wake_up(&vb_buf->vb.done);

		/* re-set the buffer timeout */
		mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);

	} while (0);

	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);
}

static void sc0710_dma_dequeue_audio(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain)
{
	struct sc0710_dma_descriptor_chain_allocation *dca = &chain->allocations[0];
	int samplesPerChannel;
	int stride = 16;
	int ret;
	int i;

	if (chain->numAllocations != 1) {
		printk("%s() allocations should be one, dma issue?\n", __func__);
	}

	for (i = 0; i < chain->numAllocations; i++) {

		samplesPerChannel = dca->buf_size / stride;

		ret = sc0710_audio_deliver_samples(ch->dev, ch,
			(const u8 *)dca->buf_cpu,
			16,     /* bitwidth */
			stride,
			2,      /* channels */
			samplesPerChannel);

		dca++;
	}

}

int sc0710_dma_channel_service(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_dma_descriptor_chain_allocation *dca;
	struct sc0710_dma_descriptor_chain *chain;
	u32 wbm[2];
	u32 v;
	int i;

	if (ch->enabled == 0)
		return -1;

	v = sc_read(ch->dev, 1, ch->reg_dma_completed_descriptor_count);

	if (v == ch->dma_completed_descriptor_count_last) {
		/* No new buffers since our last service call. */
		return 0;
	}

	dprintk(3, "ch#%d    was %d now %d\n", ch->nr, ch->dma_completed_descriptor_count_last, v);
	ch->dma_completed_descriptor_count_last = v;

	for (i = 0; i < ch->numDescriptorChains; i++) {
		chain = &ch->chains[i];

		/* Last allocated SG buffer in the chain. */
		dca = &chain->allocations[ chain->numAllocations - 1 ];

		wbm[0] = *dca->wbm[0];
		wbm[1] = *dca->wbm[1];

		/* If the write back metadata is set, we know the chain is complete. */
		if (wbm[0] && wbm[1]) {

			if (dma_channel_debug > 2) {
				printk("%s ch#%d    [%02d] %08x - wbm %08x %08x (DQ) segs: %d\n",
					ch->dev->name,
					ch->nr,
					i,
					dca->desc->control,
					wbm[0],
					wbm[1], chain->numAllocations);
			}

			sc0710_things_per_second_update(&ch->bitsPerSecond, chain->total_transfer_size * 8);
			sc0710_things_per_second_update(&ch->descPerSecond, chain->numAllocations);

			if (ch->mediatype == CHTYPE_VIDEO) {
				sc0710_dma_dequeue_video(ch, chain);
			} else
			if (ch->mediatype == CHTYPE_AUDIO) {
				sc0710_dma_dequeue_audio(ch, chain);
			}

			/* Reset the descriptor state so we know when it's complete next time. */
			*(dca->wbm[0]) = 0;
			*(dca->wbm[1]) = 0;
		}
	}

	return 0; /* Success */
}

/* Build the scatter gather table chaining all of the chains and decriptors together. */
static int sc0710_dma_channel_chains_link(struct sc0710_dma_channel *ch)
{
	struct sc0710_dma_descriptor_chain *chain;
	struct sc0710_dma_descriptor_chain_allocation *dca;
	struct sc0710_dma_descriptor *pt_desc = (struct sc0710_dma_descriptor *)ch->pt_cpu;
	dma_addr_t curr_tbl = ch->pt_dma;
	dma_addr_t curr_wbm = ch->pt_dma + PAGE_SIZE;
	int i, j;

	/* Now that we have all of the dma allocations, we can update the descriptor tables with DMA io addresses. */
	for (i = 0; i < ch->numDescriptorChains; i++) {
		chain = &ch->chains[i];

		for (j = 0; j < chain->numAllocations; j++) {
			dca = &chain->allocations[j];

			dca->desc = pt_desc++;

			if ((i + 1 == ch->numDescriptorChains) && (j + 1 == chain->numAllocations)) {
				/* Last descriptor in the last chains needs to point to the first desc in first chain. */
				dca->desc->next_l = (u64)ch->pt_dma;
				dca->desc->next_h = (u64)ch->pt_dma >> 32;
			} else {
				dca->desc->next_l = (u64)curr_tbl + sizeof(struct sc0710_dma_descriptor);
				dca->desc->next_h = ((u64)curr_tbl + sizeof(struct sc0710_dma_descriptor)) >> 32;
			}

			dca->desc->control     = 0xAD4B0000;
			dca->desc->lengthBytes = dca->buf_size;
			dca->desc->src_l       = (u64)curr_wbm;
			dca->desc->src_h       = (u64)curr_wbm >> 32;
			dca->desc->dst_l       = (u64)dca->buf_dma;
			dca->desc->dst_h       = (u64)dca->buf_dma >> 32;

			dca->wbm[0]            = bus_to_virt(curr_wbm);
			dca->wbm[1]            = bus_to_virt(curr_wbm) + sizeof(u32);

			curr_tbl += sizeof(struct sc0710_dma_descriptor);
			curr_wbm += sizeof(struct sc0710_dma_descriptor);
		}

	}

	return 0; /* Success */
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

	spin_lock_init(&ch->v4l2_capture_list_lock);
	INIT_LIST_HEAD(&ch->v4l2_capture_list);

	ch->dev = dev;
	ch->nr = nr;
	ch->enabled = 1;
	ch->direction = direction;
	ch->mediatype = mediatype;
	ch->state = STATE_STOPPED;
	sc0710_things_per_second_reset(&ch->bitsPerSecond);
	sc0710_things_per_second_reset(&ch->descPerSecond);
	sc0710_things_per_second_reset(&ch->audioSamplesPerSecond);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ch->numDescriptorChains = 4;
		ch->buf_size = 0x1c2000; /* 1280x 720p*/
		ch->buf_size = 0x3f4800; /* 1920x1080p */
		//ch->buf_size = 0xfd2000; /* 3840x2160p */
printk("Allocating channel for size %d\n", ch->buf_size);
	} else
	if (ch->mediatype == CHTYPE_AUDIO) {
		ch->numDescriptorChains = 4;
		ch->buf_size = 0x4000;
	} else {
		ch->numDescriptorChains = 0;
	}

	/* Page table defaults. */
	/* This assumed PAGE_SIZE is 4K */
	/* allocate the descriptor table, its contigious. */
	ch->pt_size = PAGE_SIZE * 2;

	ch->pt_cpu = pci_alloc_consistent(dev->pci, ch->pt_size, &ch->pt_dma);
	if (ch->pt_cpu == 0)
		return -1;

	memset(ch->pt_cpu, 0, ch->pt_size);

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

	sc0710_dma_chains_alloc(ch, ch->buf_size);

	printk(KERN_INFO "%s channel %d allocated\n", dev->name, nr);

	sc0710_dma_channel_chains_link(ch);
	sc0710_dma_chains_dump(ch);

	if (ch->mediatype == CHTYPE_VIDEO) {
		ret = sc0710_video_register(ch);
	}
	if (ch->mediatype == CHTYPE_AUDIO) {
		sc0710_audio_register(dev);
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
		sc0710_video_unregister(ch);
	}
	if (ch->mediatype == CHTYPE_AUDIO) {
		sc0710_audio_unregister(dev);
	}

	sc0710_dma_chains_free(ch);

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
	ch->state = STATE_STOPPED;
	return 0;
}

int sc0710_dma_channel_start(struct sc0710_dma_channel *ch)
{
	sc_write(ch->dev, 1, ch->reg_dma_control_w1s, 0x00000001);
	ch->state = STATE_RUNNING;
	return 0;
}

enum sc0710_channel_state_e sc0710_dma_channel_state(struct sc0710_dma_channel *ch)
{
	return ch->state;
}

