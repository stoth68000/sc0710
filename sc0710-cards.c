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

#include "sc0710.h"

struct sc0710_board sc0710_boards[] = {
	[SC0710_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
		/* Ensure safe default for unknown boards */
	},
	[SC0710_BOARD_ELGATEO_4KP60_MK2] = {
		.name		= "Elgato 4k60 Pro mk.2",
	},
};
const unsigned int sc0710_bcount = ARRAY_SIZE(sc0710_boards);

struct sc0710_subid sc0710_subids[] = {
	{
		.subvendor = 0x1cfa,
		.subdevice = 0x000e,
		.card      = SC0710_BOARD_ELGATEO_4KP60_MK2,
	}
};
const unsigned int sc0710_idcount = ARRAY_SIZE(sc0710_subids);

void sc0710_card_list(struct sc0710_dev *dev)
{
	int i;

	if (0 == dev->pci->subsystem_vendor &&
	    0 == dev->pci->subsystem_device) {
		printk(KERN_INFO
			"%s: Board has no valid PCIe Subsystem ID and can't\n"
		       "%s: be autodetected. Pass card=<n> insmod option\n"
		       "%s: to workaround that. Redirect complaints to the\n"
		       "%s: vendor of the TV card.  Best regards,\n"
		       "%s:         -- tux\n",
		       dev->name, dev->name, dev->name, dev->name, dev->name);
	} else {
		printk(KERN_INFO
			"%s: Your board isn't known (yet) to the driver.\n"
		       "%s: Try to pick one of the existing card configs via\n"
		       "%s: card=<n> insmod option.  Updating to the latest\n"
		       "%s: version might help as well.\n",
		       dev->name, dev->name, dev->name, dev->name);
	}
	printk(KERN_INFO "%s: Here is a list of valid choices for the card=<n> insmod option:\n",
	       dev->name);
	for (i = 0; i < sc0710_bcount; i++)
		printk(KERN_INFO "%s:    card=%d -> %s\n",
		       dev->name, i, sc0710_boards[i].name);
}

void sc0710_gpio_setup(struct sc0710_dev *dev)
{
	switch (dev->board) {
	case SC0710_BOARD_ELGATEO_4KP60_MK2:
		break;
	}
}

void sc0710_card_setup(struct sc0710_dev *dev)
{
	switch (dev->board) {
	case SC0710_BOARD_ELGATEO_4KP60_MK2:
#if 0
		printk("configuring regs\n");
		v = sc_read(dev, 0, BAR0_2000); printk("2000 = %08x\n", v);
		v = sc_read(dev, 0, BAR0_3000); printk("3000 = %08x\n", v);
#endif

		sc_write(dev, 0, BAR0_00C4, 0x000f0000);

#if 0
		v = sc_read(dev, 1, BAR1_2000); printk("2000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_3000); printk("3000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_0000); printk("0000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_4000); printk("4000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_0100); printk("0100 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_4100); printk("4100 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_0200); printk("0200 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_4200); printk("4200 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_0300); printk("0300 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_4300); printk("4300 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_1000); printk("1000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_5000); printk("5000 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_1100); printk("1100 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_5100); printk("5100 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_1200); printk("1200 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_5200); printk("5200 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_1300); printk("1300 = %08x\n", v);
		v = sc_read(dev, 1, BAR1_5300); printk("5300 = %08x\n", v);
		v = sc_read(dev, 0, BAR0_0008); printk("0008 = %08x\n", v);
#endif

		sc_write(dev, 1, BAR1_0094, 0x00fffe3e);
		sc_write(dev, 1, BAR1_0008, 0x00fffe3e);
		sc_write(dev, 1, BAR1_0194, 0x00fffe3e);
		sc_write(dev, 1, BAR1_0108, 0x00fffe3e);
		sc_write(dev, 1, BAR1_1094, 0x00fffe7e);
		sc_write(dev, 1, BAR1_1008, 0x00fffe7e);
		sc_write(dev, 1, BAR1_1194, 0x00fffe7e);
		sc_write(dev, 1, BAR1_1108, 0x00fffe7e);
		sc_write(dev, 1, BAR1_2080, 0);
		sc_write(dev, 1, BAR1_2084, 0);
		sc_write(dev, 1, BAR1_2088, 0);
		sc_write(dev, 1, BAR1_208C, 0);
		sc_write(dev, 1, BAR1_20A0, 0);
		sc_write(dev, 1, BAR1_20A4, 0);
		break;
	}
}
