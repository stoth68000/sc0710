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
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif

#include "sc0710-reg.h"

#define SC0710_VERSION_CODE KERNEL_VERSION(1, 0, 0)

#define SC0710_MAX_CHANNELS 2
#define SC0710_MAX_CHANNEL_DESCRIPTORS 8

#define UNSET (-1U)

#define SC0710_MAXBOARDS 8

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

struct sc0710_dma_channel
{
	struct sc0710_dev           *dev;
        u32                          nr;
	u32                          enabled;
	enum sc0710_channel_dir_e    direction;
	enum sc0710_channel_type_e   mediatype;

	struct mutex                 lock;
	u32                          numDescriptors;
	struct sc0710_dma_descriptor descs[SC0710_MAX_CHANNEL_DESCRIPTORS];

	/* PCI BAR addresses */
	u32                          register_dma_base;
	u32                          register_sg_base;
	u32                          reg_sg_start_l;
	u32                          reg_sg_start_h;
	u32                          reg_sg_adj;

	/* A single page hold the entire descriptor list for a channel. */
	u32                          pt_size; /* PCI allocation size in bytes */
	u64                         *pt_cpu;  /* Virtual address */
	dma_addr_t                   pt_dma;  /* Physical address - accessible to the PCIe endpoint */

	/* A single large allocation holds an entire video frame, or audio buffer. */
	u32                          buf_size; /* PCI allocation size in bytes */
	u64                         *buf_cpu[SC0710_MAX_CHANNEL_DESCRIPTORS];  /* Virtual address */
	dma_addr_t                   buf_dma[SC0710_MAX_CHANNEL_DESCRIPTORS];  /* Physical address - accessible to the PCIe endpoint */
};

struct sc0710_i2c {
	int nr;
	struct sc0710_dev *dev;

	struct i2c_adapter         i2c_adap;
	struct i2c_client          i2c_client;
	u32                        i2c_rc;
};

struct sc0710_format_s
{
	u32   timingH;
	u32   timingV;
	u32   width;
	u32   height;
	u32   interlaced;
	u32   fpsX100;
	u32   fpsnum;
	u32   fpsden;
	char *name;
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
 	struct task_struct         *kthread;
	struct mutex               kthread_lock;

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

	/* Procamp */
	s32                        brightness;
	s32                        contrast;
	s32                        saturation;
	s32                        hue;
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
int sc0710_i2c_hdmi_status_dump(struct sc0710_dev *dev);
int sc0710_i2c_read_hdmi_status(struct sc0710_dev *dev);
int sc0710_i2c_read_status2(struct sc0710_dev *dev);
int sc0710_i2c_read_status3(struct sc0710_dev *dev);
int sc0710_i2c_read_procamp(struct sc0710_dev *dev);

/* -video.c */
const struct sc0710_format_s *sc0710_format_find_by_timing(u32 timingH, u32 timingV);

/* -dma-channel.c */
int  sc0710_dma_channel_alloc(struct sc0710_dev *dev, u32 nr, enum sc0710_channel_dir_e direction, u32 baseaddr,
	enum sc0710_channel_type_e mediatype);

void sc0710_dma_channel_free(struct sc0710_dev *dev, u32 nr);
void sc0710_dma_channel_descriptors_dump(struct sc0710_dma_channel *ch);

