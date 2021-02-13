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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include "sc0710.h"

static struct sc0710_format formats[] =
{
	{  858,  262,  720,  240, 1, 2997, 30000, 1001, 8, 0, "720x480i29.97" },
	{  858,  525,  720,  480, 0, 5994, 60000, 1001, 8, 0, "720x480p59.94" },

	{  864,  312,  720,  288, 1, 2500, 25000, 1000, 8, 0, "720x576i25" },

	{ 1980,  750, 1280,  720, 0, 5000, 50000, 1000, 8, 0, "1280x720p50" },
	{ 1650,  750, 1280,  720, 0, 5994, 60000, 1001, 8, 0, "1280x720p59.94" },
	{ 1650,  750, 1280,  720, 0, 6000, 60000, 1000, 8, 0, "1280x720p60" },

	{ 2640,  562, 1920,  540, 1, 2500, 25000, 1000, 8, 0, "1920x1080i25" },
	{ 2200,  562, 1920,  540, 1, 2997, 30000, 1001, 8, 0, "1920x1080i29.97" },
	{ 2750, 1125, 1920, 1080, 0, 2400, 24000, 1000, 8, 0, "1920x1080p24" },
	{ 2640, 1125, 1920, 1080, 0, 2500, 25000, 1000, 8, 0, "1920x1080p25" },
	{ 2200, 1125, 1920, 1080, 0, 3000, 30000, 1000, 8, 0, "1920x1080p30" },
	{ 2640, 1125, 1920, 1080, 0, 5000, 50000, 1000, 8, 0, "1920x1080p50" },
	{ 2200, 1125, 1920, 1080, 0, 6000, 60000, 1000, 8, 0, "1920x1080p60" },

	{ 4400, 2250, 3840, 2160, 0, 6000, 60000, 1000, 8, 0, "3840x2160p60" },
};

void sc0710_format_initialize(void)
{
	struct sc0710_format *fmt;
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		fmt = &formats[i];

		/* Assuming YUV 8-bit */
		fmt->framesize = fmt->width * 2 * fmt->height;
	}
}

const struct sc0710_format *sc0710_format_find_by_timing(u32 timingH, u32 timingV)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if ((formats[i].timingH == timingH) && (formats[i].timingV == timingV)) {
			return &formats[i];
		}
	}

	return NULL;
}
