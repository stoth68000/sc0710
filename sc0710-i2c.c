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
#include <linux/delay.h>
#include <asm/io.h>

#include "sc0710.h"

#define I2C_DEV__ARM_MCU (0x64)

static u8 busread(struct sc0710_dev *dev)
{
	u32 v;
	int cnt = 16;

	while (cnt-- > 0) {
		v = sc_read(dev, 0, BAR0_3104);
//printk("readbus %08x\n", v);
		if ((v == 0x0000008c) || (v == 0x000000ac))
			break;
		udelay(500);
	}

	v = sc_read(dev, 0, BAR0_310C);
//printk("readbus ret 0x%02x\n", v);
	return v;
}

#if 0
int sc0710_i2c_hdmi_status_dump(struct sc0710_dev *dev)
{
	u32 v;
	u8 i2c_devaddr = 0x64; /* From dev 64, read 0x1a bytes from subaddress 0 */
	u8 i2c_readlen = 0x1a;
	u8 i2c_subaddr = 0x00;
	u8 dat[0x1a];
	int cnt = 16;

	mutex_lock(&dev->signalMutex);

	/* This is a write read transaction, taken from the ISC bus via analyzer.
	 * 7 bit addressing (0x32 is 0x64)
	 * write to 0x32 ack data: 0x00 
	 *  read to 0x32 ack data: 0x00 0x00 0x00 0x00 0x32 0x02 0x98 0x08 0x1C 0x02 0x80 0x07 0x00 0x11 0x02 0x01 0x01 0x01 0x00 0x80 0x80 0x80 0x80 0x00 0x00 0x00
	 *                                             <= 562==> <=2200==> <= 540==> <=1920==>         ^ bit 1 flipped - interlaced?
	 */

	sc_write(dev, 0, BAR0_3100, 0x00000002);
	sc_write(dev, 0, BAR0_3100, 0x00000001);
	sc_write(dev, 0, BAR0_3108, 0x00000100 | i2c_devaddr);
	/*                                ^^   I'm guess this is number of bytes to write */

	/* Wait for the device ack */
	while (cnt > 0) {
		v = sc_read(dev, 0, BAR0_3104);
		if (v == 0x00000044)
			break;
		udelay(64);
		cnt--;
	}
	//dprintk(0, "Read 3104 %08x at cnt %d -- 44?\n", v, cnt);
	if (cnt <= 0) {
		mutex_unlock(&dev->signalMutex);
		return 0;
	}

	/* Write out subaddress (single byte) */
	sc_write(dev, 0, BAR0_3108, 0x00000000 | i2c_subaddr);

	/* Wait for the device ack */
	cnt = 16;
	while (cnt > 0) {
		v = sc_read(dev, 0, BAR0_3104);
		if (v == 0x000000c4)
			break;
		udelay(64);
		cnt--;
	}
	//dprintk(0, "Read 3104 %08x at cnt %d -- c4?\n", v, cnt);

	udelay(2000); // pkt 15162
	sc_write(dev, 0, BAR0_3120, 0x0000000f);
	sc_write(dev, 0, BAR0_3100, 0x00000002);
	sc_write(dev, 0, BAR0_3100, 0x00000000);
	sc_write(dev, 0, BAR0_3108, 0x00000100 | (i2c_devaddr | 1)); /* Read from 0x65 */
	sc_write(dev, 0, BAR0_3108, 0x00000200 | i2c_readlen);
	sc_write(dev, 0, BAR0_3100, 0x00000001);

	/* Read the reply */
	cnt = 0;
	while (cnt < 0x1a) {
		dat[cnt] = busread(dev);
		//printk("dat[0x%02x] %02x\n", cnt, dat[cnt]); 
		cnt++;
	}
	v = sc_read(dev, 0, BAR0_3104);
	if (v != 0xc8) {
		printk("3104 %08x --- c8?\n", sc_read(dev, 0, BAR0_3104));
		printk("  ac %08x --- 0?\n", sc_read(dev, 0, BAR0_00AC));
		return -1;
	}

#if 0
	//  read to 0x32 ack data: 0x00 0x00 0x00 0x00 0xEE 0x02 0x72 0x06 0xD0 0x02 0x00 0x05 0x00 0x10 0x02 0x01 0x01 0x01 0x00 0x80 0x80 0x80 0x80 0x00 0x00 0x00
	//                                             <= 750==> <=1650==> <= 720==> <=1280==>
	//
	printk("%s HDMI: ", dev->name);
	for (cnt = 0; cnt < 0x1a; cnt++)
		printk("%02x ", dat[cnt]);
	printk("\n");
#endif

	if (dat[8]) {
		dev->locked = 1;
		dev->width = dat[0x0b] << 8 | dat[0x0a];
		dev->height = dat[0x09] << 8 | dat[0x08];
		dev->pixelLineV = dat[0x05] << 8 | dat[0x04];
		dev->pixelLineH = dat[0x07] << 8 | dat[0x06];

		dev->interlaced = dat[0x0d] & 0x01;
		if (dev->interlaced)
			dev->height *= 2;

	} else {
		dev->locked = 0;
		dev->width = 0;
		dev->height = 0;
		dev->pixelLineH = 0;
		dev->pixelLineV = 0;
		dev->interlaced = 0;
	}

#if 0
	if (dev->locked) {	
		printk("%s HDMI: %dx%d%c (%dx%d)\n", dev->name,
			dev->width, dev->height,
			dev->interlaced ? 'i' : 'p',
			dev->pixelLineH, dev->pixelLineV);
	} else {
		printk("%s HDMI: no signal\n", dev->name);
	}
#endif

	mutex_unlock(&dev->signalMutex);
	return 0;
}
#endif

static int sc0710_i2c_writeread(struct sc0710_dev *dev, u8 devaddr8bit, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	u32 v;
	u8 i2c_devaddr = devaddr8bit; /* From dev 64, read 0x1a bytes from subaddress 0 */
	u8 i2c_readlen = rlen;
	u8 i2c_subaddr = wbuf[0];
	int cnt = 16;

	mutex_lock(&dev->signalMutex);

	/* This is a write read transaction, taken from the ISC bus via analyzer.
	 * 7 bit addressing (0x32 is 0x64)
	 * write to 0x32 ack data: 0x00 
	 *  read to 0x32 ack data: 0x00 0x00 0x00 0x00 0x32 0x02 0x98 0x08 0x1C 0x02 0x80 0x07 0x00 0x11 0x02 0x01 0x01 0x01 0x00 0x80 0x80 0x80 0x80 0x00 0x00 0x00
	 *                                             <= 562==> <=2200==> <= 540==> <=1920==>         ^ bit 1 flipped - interlaced?
	 */

	sc_write(dev, 0, BAR0_3100, 0x00000002);
	sc_write(dev, 0, BAR0_3100, 0x00000001);
	sc_write(dev, 0, BAR0_3108, 0x00000000 | (wlen << 8) | i2c_devaddr);

	/* Wait for the device ack */
	while (cnt > 0) {
		v = sc_read(dev, 0, BAR0_3104);
		if (v == 0x00000044)
			break;
		udelay(64);
		cnt--;
	}
	//dprintk(0, "Read 3104 %08x at cnt %d -- 44?\n", v, cnt);
	if (cnt <= 0) {
		mutex_unlock(&dev->signalMutex);
		return 0;
	}

	/* Write out subaddress (single byte) */
	/* TODO: We only suppport single byte sub-addresses. */
	sc_write(dev, 0, BAR0_3108, 0x00000000 | i2c_subaddr);

	/* Wait for the device ack */
	cnt = 16;
	while (cnt > 0) {
		v = sc_read(dev, 0, BAR0_3104);
		if (v == 0x000000c4)
			break;
		udelay(64);
		cnt--;
	}
	//dprintk(0, "Read 3104 %08x at cnt %d -- c4?\n", v, cnt);

	udelay(2000); // pkt 15162
	sc_write(dev, 0, BAR0_3120, 0x0000000f);
	sc_write(dev, 0, BAR0_3100, 0x00000002);
	sc_write(dev, 0, BAR0_3100, 0x00000000);
	sc_write(dev, 0, BAR0_3108, 0x00000100 | (i2c_devaddr | 1)); /* Read from 0x65 */
	sc_write(dev, 0, BAR0_3108, 0x00000200 | i2c_readlen);
	sc_write(dev, 0, BAR0_3100, 0x00000001);

	/* Read the reply */
	cnt = 0;
	while (cnt < i2c_readlen) {
		*(rbuf + cnt) = busread(dev);
		//printk("dat[0x%02x] %02x\n", cnt, dat[cnt]); 
		cnt++;
	}
	v = sc_read(dev, 0, BAR0_3104);
	if (v != 0xc8) {
		printk("3104 %08x --- c8?\n", sc_read(dev, 0, BAR0_3104));
		printk("  ac %08x --- 0?\n", sc_read(dev, 0, BAR0_00AC));
		mutex_unlock(&dev->signalMutex);
		return -1;
	}

	mutex_unlock(&dev->signalMutex);
	return 0; /* Success */
}

int sc0710_i2c_read_hdmi_status(struct sc0710_dev *dev)
{
	int ret;
	u8 wbuf[1]    = { 0x00 /* Subaddress */ };
	u8 rbuf[0x1a] = { 0    /* response buffer */};

	ret = sc0710_i2c_writeread(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf), &rbuf[0], sizeof(rbuf));
	printk("%s ret = %d\n", __func__, ret);

	if (rbuf[8]) {
		dev->locked = 1;
		dev->width = rbuf[0x0b] << 8 | rbuf[0x0a];
		dev->height = rbuf[0x09] << 8 | rbuf[0x08];
		dev->pixelLineV = rbuf[0x05] << 8 | rbuf[0x04];
		dev->pixelLineH = rbuf[0x07] << 8 | rbuf[0x06];

		dev->interlaced = rbuf[0x0d] & 0x01;
		if (dev->interlaced)
			dev->height *= 2;

	} else {
		dev->locked = 0;
		dev->width = 0;
		dev->height = 0;
		dev->pixelLineH = 0;
		dev->pixelLineV = 0;
		dev->interlaced = 0;
	}

	return 0; /* Success */
}

int sc0710_i2c_read_status2(struct sc0710_dev *dev)
{
	int ret, i;
	u8 wbuf[1]    = { 0x1a /* Subaddress */ };
	u8 rbuf[0x10] = { 0    /* response buffer */};

	ret = sc0710_i2c_writeread(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf), &rbuf[0], sizeof(rbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	printk("%s status2: ", dev->name);
	for (i = 0; i < sizeof(rbuf); i++)
		printk("%02x ", rbuf[i]);
	printk("\n");

	return 0; /* Success */
}

int sc0710_i2c_read_status3(struct sc0710_dev *dev)
{
	int ret, i;
	u8 wbuf[1]    = { 0x2a /* Subaddress */ };
	u8 rbuf[0x10] = { 0    /* response buffer */};

	ret = sc0710_i2c_writeread(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf), &rbuf[0], sizeof(rbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	printk("%s status3: ", dev->name);
	for (i = 0; i < sizeof(rbuf); i++)
		printk("%02x ", rbuf[i]);
	printk("\n");

	return 0; /* Success */
}

/* User video controls for brightness, contrast, saturation and hue. */
int sc0710_i2c_read_procamp(struct sc0710_dev *dev)
{
	int ret, i;
	u8 wbuf[1]    = { 0x12 /* Subaddress */ };
	u8 rbuf[0x05] = { 0    /* response buffer */};

	ret = sc0710_i2c_writeread(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf), &rbuf[0], sizeof(rbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	dev->brightness = rbuf[1];
	dev->contrast   = rbuf[2];
	dev->saturation = rbuf[3];
	dev->hue        = (s8)rbuf[4];

	printk("%s procamp: ", dev->name);
	for (i = 0; i < sizeof(rbuf); i++)
		printk("%02x ", rbuf[i]);
	printk("\n");

	printk("%s procamp: brightness %d contrast %d saturation %d hue %d\n",
		dev->name,
		dev->brightness,
		dev->contrast,
		dev->saturation,
		dev->hue);

	return 0; /* Success */
}

