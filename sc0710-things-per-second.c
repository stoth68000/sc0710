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

void sc0710_things_per_second_reset(struct sc0710_things_per_second *tps)
{
	getnstimeofday(&tps->lastTime);
	tps->persecond = 0;
	tps->accumulator = 0;
}

void sc0710_things_per_second_update(struct sc0710_things_per_second *tps, s64 value)
{
	struct timespec now;
	getnstimeofday(&now);

	if (tps->lastTime.tv_sec != now.tv_sec) {
		tps->lastTime = now;
		tps->persecond = tps->accumulator;
		tps->accumulator = 0;
	}
	tps->accumulator += value;
}

s64 sc0710_things_per_second_query(struct sc0710_things_per_second *tps)
{
	return tps->persecond;
}

