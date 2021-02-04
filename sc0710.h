/*
 * Copyright 2020 - Kernel Labs Inc. www.kernellabs.com.
 * 
 * The entire content of this file is considered proprietary, confidential
 * and closed source by Kernel Labs Inc.
 * 
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
	u32                        __iomem *lmmio;
	u8                         __iomem *bmmio;

	/* A kernel thread to keep the HDMI video frontend alive. */
 	struct task_struct	*kthread;
	struct mutex 		kthread_lock;

};

/* ----------------------------------------------------------- */

#define sc_read(reg)             readl(dev->lmmio + ((reg)>>2))
#define sc_write(reg, value)     { writel((value), dev->lmmio + ((reg)>>2)); /* printk(KERN_ERR "w:%x(%x)\n", reg, value ); */ } 

#if 0
#define tm_andor(reg, mask, value) \
  writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
  ((value) & (mask)), dev->lmmio+((reg)>>2)) ; /* printk(KERN_ERR "ao:%x(%x, %x) %x\n", reg, value, mask, readl(dev->lmmio+((reg)>>2))) */
#else
#define sc_andor(reg, mask, value)\
{\
	u32 newval = (readl(dev->lmmio+((reg)>>2)) & ~(mask)) | ((value) & (mask));\
	writel(newval, dev->lmmio+((reg)>>2));\
/*	printk(KERN_ERR "ao:%x(%x)\n", reg, newval); */\
}
#endif

#define sc_set(reg, bit)          sc_andor((reg), (bit), (bit))
#define sc_clear(reg, bit)        sc_andor((reg), (bit), 0)

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
