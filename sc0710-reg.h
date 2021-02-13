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

/* PCIe bar 0 - 1MB in length */

/* Undefined */
#define BAR0_0000 0x0000 /* 10ee7021 */
#define BAR0_0004 0x0004 /* 00020001 */

/* Firmware version: 20190808 = 2019.8.8 */
#define BAR0_0008 0x0008

/*
 * 0x0a8  when idle the value is zero. and it goes to zero if the capture app terminates
 *        when running, with 1080p30, it contains 04380000 and the lowest bit 0 toggles high and low
 *        at unpredictable times.
 *        0438 is 1080 decimal.
 *        00f00000 when the tivo is booting (app reports source is 480p)
 *        01680000 (with lower bit toggling) when tivo connected at 720p60
 *        010e0000 (with lower bit toggling) when tivo at 1080i59   (270)
 *        00780000 with 480i60 tivo input
 */
#define BAR0_00A8 0x00a8

/*
 * 0x0ac  seems to hold 06400000 and occasionally 06410000 when the card is idle
 *        03200000 when the tivo is booting (480p)
 *        03200000 when tivo booted to 720p60 or 1080i59
 *        03200000 when tivo running 480i60
 *        03210000 (or 03200000)when tivo 48-i and card idle.
 */
#define BAR0_00AC 0x00aC

/* 0x0c4  00f0000 when idle or stream, appears fixed. */
#define BAR0_00C4 0x00c4

/*
 * 0x0c8  Contains HDMI source heigh?
 *        When running with 2160p source.
 *        The value contains 00000870 (2160 in hex). and 0xa8 contains 1080, suggesting the card is scaling
 *        to 1080p output, the capture app says 1080p capture (source 2160p).
 *        Contains:
 *        000001e0 when tivo booting 480p60 (480)
 *        000002d0 when tivo at 720p60
 *        00000438 when tivo at 1080i59
 *        000001e0 when tivo 480i
 */
#define BAR0_00C8 0x00c8

/* 0x00d0   00004100 when card idle 00004101 when streaming, no matter what format is active on hdmi. */
#define BAR0_00D0 0x00d0

/*
 * 0x00d4 000dbbbf or 000dbba0 (mostly a0) (source 2160p)
 *        0006ddcf or 0006ddd0(?) when tivo 2170p60 connected or tivo 1080i
 *        0006df91 or 0006df92    when tivo 480i
 */
#define BAR0_00D4 0x00d4

/*
 * 0x00d8 00000438 when source was 2160p.
 *        00000168 when source 720p60
 *        0000010e when tivo was connected as 1080i59
 *        00000078 when tivo 480i decimal 120
 */
#define BAR0_00D8 0x00d8
#define BAR0_00DC 0x00dc

/* 0x00e4 00000001 when streaming or 00000000 when idle. */
#define BAR0_00E4 0x00e4

#define BAR0_2000 0x2000
#define BAR0_3000 0x3000

/* I2C - Some type of generate I2C state machine register.
 * At various states it contains data in bits 31:24
 * 0x00 << 24
 * 0x01 << 24
 * 0x02 << 24
 *
 * 
 */
#define BAR0_3100 0x3100

/* I2C bus status
 *  It's polled before i2c reads occur to check the state
 *  of the i2c bus. All activity occurs in bits 31:24.
 *  Almost certainly, these bigs represent the i2c transaction
 *  bits of start and stop bits, ack, etc.
 *  0x44 << 24   0100 | 0100   - during read, byte not yet read on the bus.
 *  0x8C << 24   1000 | 1100   - during read, byte ready and available to read (from reg 310c)
 *  0xAC << 24   1010 | 1100   - during read, at the start of a xact, byte ready and available to read (from reg 310c)
 *  0xC8 << 24   1100 | 1000   - at the end of a long read, no more bytes available
 *  0xCC << 24   1100 | 1100   - during read, byte not yet read on the bus.
 */
#define BAR0_3104 0x3104

/* I2C data out register
 * bits 31:24 output byte (Eg 8-bit device address)
 * bits 23:16 contain the value 0x01 or 0x02
 */
#define BAR0_3108 0x3108

/* I2C data register using during sequential reads
 * Data appears as a byte in bits 31:24
 * */
#define BAR0_310C 0x310c

/* Used during i2c reads. upper 31:24 contains 0x0f.
 * Only an 0x0f ever gets written to it, and its never read.
 * This suggests its an interrupt control enable, or a bus
 * state-machine reset and enable.
 * 0x0f << 24
 */
#define BAR0_3120 0x3120

/* End: I2C */

/* PCIe bar 1 - 64KB in length */
#define BAR1_0008 0x0008
#define BAR1_0094 0x0094
#define BAR1_0100 0x0100
#define BAR1_0108 0x0108
#define BAR1_0194 0x0194
#define BAR1_0200 0x0200
#define BAR1_0300 0x0300
#define BAR1_1008 0x1008
#define BAR1_1094 0x1094
#define BAR1_1108 0x1108
#define BAR1_1194 0x1194

#define BAR1_2000 0x2000
#define BAR1_2080 0x2080
#define BAR1_2084 0x2084
#define BAR1_2088 0x2088
#define BAR1_208C 0x208c
#define BAR1_20A0 0x20a0
#define BAR1_20A4 0x20a4

#define BAR1_0000 0x0000
#define BAR1_1000 0x1000
#define BAR1_1100 0x1100
#define BAR1_1200 0x1200
#define BAR1_1300 0x1300
#define BAR1_3000 0x3000
#define BAR1_4000 0x4000
#define BAR1_4100 0x4100
#define BAR1_4200 0x4200
#define BAR1_4300 0x4300
#define BAR1_5000 0x5000
#define BAR1_5100 0x5100
#define BAR1_5200 0x5200
#define BAR1_5300 0x5300
