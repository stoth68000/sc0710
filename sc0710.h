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

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/kdev_t.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
#else
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>
#endif
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/videobuf-vmalloc.h>
#include <media/rc-core.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
#include <sound/tlv.h>
#endif
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif

#include "sc0710-reg.h"

#define SC0710_VERSION_CODE KERNEL_VERSION(1, 0, 0)

#define SC0710_MAX_CHANNELS 2

/* A chain contains 1..SC0710_MAX_CHAIN_DESCRIPTORS descriptors,
 * multiple DMA allocations and multiple descriptors to
 * target the buffer pieces.
 */
#define SC0710_MAX_CHANNEL_DESCRIPTOR_CHAINS 4
#define SC0710_MAX_CHAIN_DESCRIPTORS 8

#define UNSET (-1U)

#define SC0710_MAXBOARDS 8

#define VBUF_TIMEOUT (HZ)

/* Max number of inputs by card */
#define MAX_SC0710_INPUT 8
#define INPUT(nr) (&sc0710_boards[dev->board].input[nr])

#define SC0710_BOARD_NOAUTO              UNSET
#define SC0710_BOARD_UNKNOWN             0
#define SC0710_BOARD_ELGATEO_4KP60_MK2   1

struct sc0710_board {
	char *name;
};

struct sc0710_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

struct sc0710_dev;

struct sc0710_things_per_second
{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,0,0)
	struct old_timespec32 lastTime;
#else
	u64 lastTime;
#endif
	u64 persecond;
	u64 accumulator;
};

/* buffer for one video frame */
struct sc0710_buffer
{
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer  vb;

	/* sc0710 specific */
	const struct sc0710_format   *fmt;
};

struct sc0710_dmaqueue {
	struct list_head       active;
	struct list_head       queued;
	struct timer_list      timeout;
	//struct btcx_riscmem    stopper;
	u32                    count;
};

struct sc0710_dma_descriptor
{
	u32 control;
	u32 lengthBytes;
	u32 src_l;
	u32 src_h;
	u32 dst_l;
	u32 dst_h;
	u32 next_l;
	u32 next_h;
} __packed;

enum sc0710_channel_dir_e
{
	CHDIR_INPUT,
	CHDIR_OUTPUT,
};

enum sc0710_channel_type_e
{
	CHTYPE_VIDEO,
	CHTYPE_AUDIO,
};

enum sc0710_channel_state_e
{
	STATE_UNDEFINED = 0,
	STATE_STOPPED,
	STATE_RUNNING
};

/* Take the size of an ideal DMA transfer (say, the size of a 4K image 3840 * 2 * 2160 bytes).
 * Fragment this into 4MB PCI allocations, so for 4K we have:
 * allocsegment = 4 * 1048576 = 4194304
 * 4K = 16588800
 * allocations = 4K / allocsegment
 */
struct sc0710_dma_descriptor_chain
{
	int         enabled;
	int         total_transfer_size;

	/* Multiple DMA allocations holding an entire video frame, or audio buffer. */
	u32                          numAllocations;
	struct sc0710_dma_descriptor_chain_allocation {
                int                           enabled;
		struct sc0710_dma_descriptor *desc;
		u32                           buf_size; /* PCI allocation size in bytes, of each allocation */
		u64                          *buf_cpu;  /* Virtual address */
		dma_addr_t                    buf_dma;  /* Physical address - accessible to the PCIe endpoint */
		u32                          *wbm[2];   /* Write back metadata where we can monitor descriptor completion */
	} allocations[SC0710_MAX_CHAIN_DESCRIPTORS];
};

struct sc0710_dma_channel
{
	struct sc0710_dev           *dev;
        u32                          nr;
	u32                          enabled;
	enum sc0710_channel_dir_e    direction;
	enum sc0710_channel_type_e   mediatype;
	enum sc0710_channel_state_e  state;

	/* A single page hold the entire descriptor list for a chain. */
	u32         pt_size; /* PCI allocation size in bytes */
	u64        *pt_cpu;  /* Virtual address */
	dma_addr_t  pt_dma;  /* Physical address - accessible to the PCIe endpoint */

	struct mutex                 lock;
	u32                          numDescriptorChains;
	u32                          buf_size;
	struct sc0710_dma_descriptor_chain chains[SC0710_MAX_CHANNEL_DESCRIPTOR_CHAINS];

	/* DMA Controller PCI BAR offsets */
	u32                          register_dma_base;
	u32                          reg_dma_completed_descriptor_count;
	u32                          reg_dma_control;
	u32                          reg_dma_control_w1s;
	u32                          reg_dma_control_w1c;
	u32                          reg_dma_status1;
	u32                          reg_dma_status2;
	u32                          reg_dma_poll_wba_l;
	u32                          reg_dma_poll_wba_h;

	/* SGDMA Channel PCI BAR offsets */
	u32                          register_sg_base;
	u32                          reg_sg_start_l;
	u32                          reg_sg_start_h;
	u32                          reg_sg_adj;
	u32                          reg_sg_credits;

	/* DMA related items we need to track. */
	u32                          dma_completed_descriptor_count_last;

	/* Statistics */
	struct sc0710_things_per_second bitsPerSecond;
	struct sc0710_things_per_second descPerSecond;
	struct sc0710_things_per_second audioSamplesPerSecond;

	/* Channel 0 */
	/* V4L2 */
	struct video_device          vdev;
	struct vb2_queue             vb2_queue;
	spinlock_t                   slock;

	/* Buffering */
	spinlock_t                   v4l2_capture_list_lock;
	struct list_head             v4l2_capture_list;
	struct timer_list            timeout;
	u32                          videousers;

	/* Channel 1 */
	struct sc0710_audio_dev     *audio_dev;
};

struct sc0710_i2c {
	int nr;
	struct sc0710_dev *dev;

	struct i2c_adapter         i2c_adap;
	struct i2c_client          i2c_client;
	u32                        i2c_rc;
};

enum sc0710_colorimetry_e
{
	BT_UNDEFINED = 0,
	BT_601  = 601,
	BT_709  = 709,
	BT_2020 = 2020,
};

enum sc0710_colorspace_e
{
	CS_UNDEFINED = 0,
	CS_YUV_YCRCB_422_420, 
	CS_YUV_YCRCB_444,
	CS_RGB_444,
};

struct sc0710_format
{
	u32   timingH;
	u32   timingV;
	u32   width;
	u32   height;
	u32   interlaced;
	u32   fpsX100;
	u32   fpsnum;
	u32   fpsden;
	u32   depth; /* bits */
	u32   framesize; /* bytes */
	char *name;
	struct v4l2_dv_timings dv_timings;
};

struct sc0710_audio_dev
{
	struct sc0710_dev         *dev;
	struct snd_pcm_substream  *substream;
	struct  snd_card          *card;
	snd_pcm_uframes_t          buffer_ptr;
};

struct sc0710_dev {
	struct list_head devlist;

	atomic_t                   refcount;

	/* board details */
	int                        nr;
	struct mutex               lock;
	unsigned int               board;
	char                       name[32];

	/* pci stuff */
	struct pci_dev             *pci;
	unsigned char              pci_rev, pci_lat;
	u32                        __iomem *lmmio[2];
	u8                         __iomem *bmmio[2];

	/* A kernel thread to keep the HDMI video frontend alive. */
 	struct task_struct         *kthread_hdmi;
	struct mutex               kthread_hdmi_lock;

	/* A kernel thread that checks the dma descriptors
	 * instead of relying on highly latent interrupts.
	 */
 	struct task_struct         *kthread_dma;
	struct mutex               kthread_dma_lock;

	/* Misc structs */
	struct sc0710_i2c          i2cbus[1];

	/* Anything channel related. */
	struct sc0710_dma_channel  channel[SC0710_MAX_CHANNELS];

	/* Signal format. Its not value to check anything without taking
	 * the mutex.
	 */
	struct mutex               signalMutex;
	u32                        locked;
	u32                        pixelLineH, pixelLineV; /* HDMI line format */
	u32                        width, height;    /* Actual display */
	u32                        interlaced;
	const struct sc0710_format *fmt;
	enum sc0710_colorimetry_e  colorimetry;
	enum sc0710_colorspace_e   colorspace;

	/* Procamp */
	s32                        brightness;
	s32                        contrast;
	s32                        saturation;
	s32                        hue;

	/* V4L2 */
	struct v4l2_device         v4l2_dev;
};

struct sc0710_fh
{
	struct v4l2_fh             fh;
	struct sc0710_dma_channel *ch;
	unsigned int               resources;
	enum v4l2_buf_type         type;
	struct videobuf_queue      vidq;
};

/* ----------------------------------------------------------- */
/* sc0710-core.c                                              */

/* ----------------------------------------------------------- */
/* sc0710-cards.c                                             */
extern struct sc0710_board sc0710_boards[];
extern const unsigned int sc0710_bcount;

extern struct sc0710_subid sc0710_subids[];
extern const unsigned int sc0710_idcount;

extern void sc0710_card_list(struct sc0710_dev *dev);
extern void sc0710_gpio_setup(struct sc0710_dev *dev);
extern void sc0710_card_setup(struct sc0710_dev *dev);

u32  sc_read(struct sc0710_dev *dev, int bar, u32 reg);
void sc_write(struct sc0710_dev *dev, int bar, u32 reg, u32 value);
void sc_set(struct sc0710_dev *dev, int bar, u32 reg, u32 bit);
void sc_clr(struct sc0710_dev *dev, int bar, u32 reg, u32 bit);

/* -i2c.c */
int sc0710_i2c_initialize(struct sc0710_dev *dev);
int sc0710_i2c_hdmi_status_dump(struct sc0710_dev *dev);
int sc0710_i2c_read_hdmi_status(struct sc0710_dev *dev);
int sc0710_i2c_read_status2(struct sc0710_dev *dev);
int sc0710_i2c_read_status3(struct sc0710_dev *dev);
int sc0710_i2c_read_procamp(struct sc0710_dev *dev);

/* -formats.c */
void sc0710_format_initialize(void);
const struct sc0710_format *sc0710_format_find_by_timing(u32 timingH, u32 timingV);

/* -dma-channel.c */
int  sc0710_dma_channel_alloc(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction, u32 baseaddr,
	enum sc0710_channel_type_e mediatype);

void sc0710_dma_channel_free(struct sc0710_dev *dev, u32 nr);
void sc0710_dma_channel_descriptors_dump(struct sc0710_dma_channel *ch);
int  sc0710_dma_channel_service(struct sc0710_dma_channel *ch);
int  sc0710_dma_channel_start_prep(struct sc0710_dma_channel *ch);
int  sc0710_dma_channel_start(struct sc0710_dma_channel *ch);
int  sc0710_dma_channel_stop(struct sc0710_dma_channel *ch);
int  sc0710_dma_channel_resize(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction, u32 baseaddr,
	enum sc0710_channel_type_e mediatype);
enum sc0710_channel_state_e sc0710_dma_channel_state(struct sc0710_dma_channel *ch);

/* --dma-channels.c */
int  sc0710_dma_channels_alloc(struct sc0710_dev *dev);
void sc0710_dma_channels_free(struct sc0710_dev *dev);
int  sc0710_dma_channels_start(struct sc0710_dev *dev);
int  sc0710_dma_channels_service(struct sc0710_dev *dev);
void sc0710_dma_channels_stop(struct sc0710_dev *dev);
int  sc0710_dma_channels_resize(struct sc0710_dev *dev);

/* things-per-second.c */
void sc0710_things_per_second_reset(struct sc0710_things_per_second *tps);
void sc0710_things_per_second_update(struct sc0710_things_per_second *tps, s64 value);
s64  sc0710_things_per_second_query(struct sc0710_things_per_second *tps);

/* video.c */
void sc0710_video_unregister(struct sc0710_dma_channel *ch);
int  sc0710_video_register(struct sc0710_dma_channel *ch);
const char *sc0710_colorimetry_ascii(enum sc0710_colorimetry_e val);
const char *sc0710_colorspace_ascii(enum sc0710_colorspace_e val);

/* -dma-chain.c */
void sc0710_dma_chain_free(struct sc0710_dma_channel *ch, int nr);
int  sc0710_dma_chain_alloc(struct sc0710_dma_channel *ch, int nr, int transfer_size);
void sc0710_dma_chain_dump(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain, int nr);
int sc0710_dma_chain_dq_to_ptr(struct sc0710_dma_channel *ch, struct sc0710_dma_descriptor_chain *chain, u8 *dst, int dstlen);

/* -dma-chains.c */
void sc0710_dma_chains_free(struct sc0710_dma_channel *ch);
int  sc0710_dma_chains_alloc(struct sc0710_dma_channel *ch, int total_transfer_size);
void sc0710_dma_chains_dump(struct sc0710_dma_channel *ch);

/* -audio.c */
int  sc0710_audio_register(struct sc0710_dev *dev);
void sc0710_audio_unregister(struct sc0710_dev *dev);
int  sc0710_audio_deliver_samples(struct sc0710_dev *dev, struct sc0710_dma_channel *ch,
        const u8 *buf, int bitdepth, int strideBytes, int channels, int samplesPerChannel);


