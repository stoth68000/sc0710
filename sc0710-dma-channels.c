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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include "sc0710.h"

int sc0710_dma_channels_alloc(struct sc0710_dev *dev)
{
	switch (dev->board) {
	case SC0710_BOARD_ELGATEO_4KP60_MK2:
		sc0710_dma_channel_alloc(dev, 0, CHDIR_INPUT, 0x1000, CHTYPE_VIDEO);
		sc0710_dma_channel_alloc(dev, 1, CHDIR_INPUT, 0x1100, CHTYPE_AUDIO);
		break;
	}

	return 0;
}

void sc0710_dma_channels_free(struct sc0710_dev *dev)
{
	int i;

	for (i = 0; i < SC0710_MAX_CHANNELS; i++) {
		sc0710_dma_channel_free(dev, i);
	}
}

void sc0710_dma_channels_stop(struct sc0710_dev *dev)
{
	int i, ret;

	printk("%s()\n", __func__);

	sc_clr(dev, 0, BAR0_00D0, 0x0001);

	for (i = 0; i < SC0710_MAX_CHANNELS; i++) {
		ret = sc0710_dma_channel_stop(&dev->channel[i]);
	}
}

int sc0710_dma_channels_start(struct sc0710_dev *dev)
{
	int i, ret;

	printk("%s()\n", __func__);

	for (i = 0; i < SC0710_MAX_CHANNELS; i++) {
		ret = sc0710_dma_channel_start_prep(&dev->channel[i]);
	}

	/* TODO: What do these registers do? Any documentation? */

	/* TODO: This register needs to be set to the height of the incoming
	 * signal format.
	 */
	sc_write(dev, 0, BAR0_00C8, 0x438);
	sc_write(dev, 0, BAR0_00D0, 0x4100);
	sc_write(dev, 0, 0xcc, 0);
	sc_write(dev, 0, 0xdc, 0);
	sc_write(dev, 0, BAR0_00D0, 0x4300);
	sc_write(dev, 0, BAR0_00D0, 0x4100);

	for (i = 0; i < SC0710_MAX_CHANNELS; i++) {
		ret = sc0710_dma_channel_start(&dev->channel[i]);
	}

	sc_set(dev, 0, BAR0_00D0, 0x0001);

	return 0;
}

int sc0710_dma_channels_service(struct sc0710_dev *dev)
{
	int i, ret;

	for (i = 0; i < SC0710_MAX_CHANNELS; i++) {
		ret = sc0710_dma_channel_service(&dev->channel[i]);
	}

	return 0;
}
