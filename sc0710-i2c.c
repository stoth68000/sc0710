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
#include <linux/delay.h>
#include <asm/io.h>

#include "sc0710.h"

#define I2C_DEV__ARM_MCU (0x32 << 1)
#define I2C_DEV__UNKNOWN (0x33 << 1)

static int didack(struct sc0710_dev *dev)
{
	u32 v;
	int cnt = 16;

	while (cnt-- > 0) {
		v = sc_read(dev, 0, BAR0_3104);
		if ((v == 0x44) || (v == 0xc0)) {
			return 1; /* device Ack'd */
		}
		udelay(64);
	}

	return 0; /* No Ack */
}

static u8 busread(struct sc0710_dev *dev)
{
	u32 v;
	int cnt = 16;

	while (cnt-- > 0) {
		v = sc_read(dev, 0, BAR0_3104);
//printk("readbus %08x\n", v);
		if ((v == 0x0000008c) || (v == 0x000000ac))
			break;
		udelay(64);
	}

	v = sc_read(dev, 0, BAR0_310C);
//printk("readbus ret 0x%02x\n", v);
	return v;
}

/* Assumes 8 bit device address and 8 bit sub address. */
static int sc0710_i2c_write(struct sc0710_dev *dev, u8 devaddr8bit, u8 *wbuf, int wlen)
{
	int i;
	u32 v;

	/* Write out to the i2c bus master a reset, then write length and device address */
	sc_write(dev, 0, BAR0_3100, 0x00000002); /* TX_FIFO Reset */
	sc_write(dev, 0, BAR0_3100, 0x00000001); /* AXI IIC Enable */
	sc_write(dev, 0, BAR0_3108, 0x00000000 | (1 << 8) /* Start Bit */ | devaddr8bit);

	/* Wait for the device ack */
	if (didack(dev) == 0)
		return -EIO;

	for (i = 1; i < wlen; i++) {
		v = 0x00000000 | *(wbuf + i);
		if (i == (wlen - 1))
			v |= (1 << 9); /* Stop Bit */
		sc_write(dev, 0, BAR0_3108, v);
		if (didack(dev) == 0)
			return -EIO;
	}

	return 0; /* Success */
}

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

	sc_write(dev, 0, BAR0_3100, 0x00000002); /* TX_FIFO Reset */
	sc_write(dev, 0, BAR0_3100, 0x00000001); /* AXI IIC Enable */
	sc_write(dev, 0, BAR0_3108, 0x00000000 | (1 << 8) /* Start Bit */ | i2c_devaddr);

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

	msleep(1); // pkt 15162
	sc_write(dev, 0, BAR0_3120, 0x0000000f);
	sc_write(dev, 0, BAR0_3100, 0x00000002); /* TX_FIFO Reset */
	sc_write(dev, 0, BAR0_3100, 0x00000000);
	sc_write(dev, 0, BAR0_3108, 0x00000000 | (1 << 8) /* Start Bit */ | (i2c_devaddr | 1)); /* Read from 0x65 */
	sc_write(dev, 0, BAR0_3108, 0x00000000 | (1 << 9) /* Stop Bit */ | i2c_readlen);
	sc_write(dev, 0, BAR0_3100, 0x00000001);

	/* Read the reply */
	cnt = 0;
	while (cnt < i2c_readlen) {
		*(rbuf + cnt) = busread(dev);
		//printk("dat[0x%02x] %02x\n", cnt, *(rbuf + cnt)); 
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
	int i;
	u8 wbuf[1]    = { 0x00 /* Subaddress */ };
	u8 rbuf[0x1a] = { 0    /* response buffer */};

	ret = sc0710_i2c_writeread(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf), &rbuf[0], sizeof(rbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	printk("%s    hdmi: ", dev->name);
	for (i = 0; i < sizeof(rbuf); i++)
		printk("%02x ", rbuf[i]);
	printk("\n");

	if (rbuf[8]) {
		dev->locked = 1;
		dev->width = rbuf[0x0b] << 8 | rbuf[0x0a];
		dev->height = rbuf[0x09] << 8 | rbuf[0x08];
		dev->pixelLineV = rbuf[0x05] << 8 | rbuf[0x04];
		dev->pixelLineH = rbuf[0x07] << 8 | rbuf[0x06];

		dev->interlaced = rbuf[0x0d] & 0x01;
		if (dev->interlaced)
			dev->height *= 2;

		dev->fmt = sc0710_format_find_by_timing(dev->pixelLineH, dev->pixelLineV);
	} else {
		dev->fmt = NULL;
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

#if 0
static int sc0710_i2c_cfg_unknownpart(struct sc0710_dev *dev)
{
	int ret;
	u8 wbuf[5] = { 0xab, 0x03, 0x12, 0x34, 0x57 };

	ret = sc0710_i2c_write(dev, I2C_DEV__UNKNOWN, &wbuf[0], sizeof(wbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	printk("%s() success\n", __func__);
	return 0; /* Success */
}

static int sc0710_i2c_cfg_unknownpart2(struct sc0710_dev *dev)
{
	int ret;
	u8 wbuf[2] = { 0x10, 0x01 };

	ret = sc0710_i2c_write(dev, I2C_DEV__ARM_MCU, &wbuf[0], sizeof(wbuf));
	if (ret < 0) {
		printk("%s ret = %d\n", __func__, ret);
		return -1;
	}

	printk("%s() success\n", __func__);
	return 0; /* Success */
}
#endif

int sc0710_i2c_initialize(struct sc0710_dev *dev)
{
	//sc0710_i2c_cfg_unknownpart2(dev);

	return 0; /* Success */
}

