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

static int video_debug = 1;

#define dprintk(level, fmt, arg...)\
        do { if (video_debug >= level)\
                printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
        } while (0)

static void sc0710_vid_timeout(unsigned long data);

const char *sc0710_colorimetry_ascii(enum sc0710_colorimetry_e val)
{
	switch (val) {
	case BT_601:       return "BT_601";
	case BT_709:       return "BT_709";
	case BT_2020:      return "BT_2020";
	default:           return "BT_UNDEFINED";
	}
}

const char *sc0710_colorspace_ascii(enum sc0710_colorspace_e val)
{
	switch (val) {
	case CS_YUV_YCRCB_422_420: return "YUV YCrCb 4:2:2 / 4:2:0";
	case CS_YUV_YCRCB_444:     return "YUV YCrCb 4:4:4";
	case CS_RGB_444:           return "RGB 4:4:4";
	default:                   return "UNDEFINED";
	}
}

#define FILL_MODE_COLORBARS 0
#define FILL_MODE_GREENSCREEN 1
#define FILL_MODE_BLUESCREEN 2
#define FILL_MODE_BLACKSCREEN 3
#define FILL_MODE_REDSCREEN 4

/* 75% IRE colorbars */
static unsigned char colorbars[7][4] =
{
	{ 0xc0, 0x80, 0xc0, 0x80 },
	{ 0xaa, 0x20, 0xaa, 0x8f },
	{ 0x86, 0xa0, 0x86, 0x20 },
	{ 0x70, 0x40, 0x70, 0x2f },
	{ 0x4f, 0xbf, 0x4f, 0xd0 },
	{ 0x39, 0x5f, 0x39, 0xe0 },
	{ 0x15, 0xe0, 0x15, 0x70 }
};
static unsigned char blackscreen[4] = { 0x00, 0x80, 0x00, 0x80 };
static unsigned char bluescreen[4] = { 0x1d, 0xff, 0x1d, 0x6b };
static unsigned char redscreen[4] = { 0x39, 0x5f, 0x39, 0xe0 };

static void fill_frame(struct sc0710_dma_channel *ch,
	unsigned char *dest_frame, unsigned int width,
	unsigned int height, unsigned int fillmode)
{
	unsigned int width_bytes = width * 2;
	unsigned int i, divider;

	if (fillmode > FILL_MODE_REDSCREEN)
		fillmode = FILL_MODE_BLACKSCREEN;

	switch (fillmode) {
	case FILL_MODE_COLORBARS:
		divider = (width_bytes / 7) + 1;
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], &colorbars[i / divider], 4);
		break;
	case FILL_MODE_GREENSCREEN:
		memset(dest_frame, 0, width_bytes);
		break;
	case FILL_MODE_BLUESCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], bluescreen, 4);
		break;
	case FILL_MODE_REDSCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], redscreen, 4);
		break;
	case FILL_MODE_BLACKSCREEN:
		for (i = 0; i < width_bytes; i += 4)
			memcpy(&dest_frame[i], blackscreen, 4);
	}

	for (i = 1; i < height; i++) {
		memcpy(dest_frame + width_bytes, dest_frame, width_bytes);
		dest_frame += width_bytes;
	}
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 0, 0)
/* Let's assume these appeared in v4.0 */

#define V4L2_DV_FL_IS_CE_VIDEO			(1 << 4)
#define V4L2_DV_FL_HAS_CEA861_VIC		(1 << 7)
#define V4L2_DV_FL_HAS_HDMI_VIC			(1 << 8)

#define V4L2_DV_BT_CEA_3840X2160P24 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 1276, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC | V4L2_DV_FL_HAS_HDMI_VIC), \
}

#define V4L2_DV_BT_CEA_3840X2160P25 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 1056, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_IS_CE_VIDEO | V4L2_DV_FL_HAS_CEA861_VIC | \
		V4L2_DV_FL_HAS_HDMI_VIC), \
}

#define V4L2_DV_BT_CEA_3840X2160P30 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		297000000, 176, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC | V4L2_DV_FL_HAS_HDMI_VIC, \
		) \
}

#define V4L2_DV_BT_CEA_3840X2160P50 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		594000000, 1056, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_IS_CE_VIDEO | V4L2_DV_FL_HAS_CEA861_VIC, ) \
}

#define V4L2_DV_BT_CEA_3840X2160P60 { \
	.type = V4L2_DV_BT_656_1120, \
	V4L2_INIT_BT_TIMINGS(3840, 2160, 0, \
		V4L2_DV_HSYNC_POS_POL | V4L2_DV_VSYNC_POS_POL, \
		594000000, 176, 88, 296, 8, 10, 72, 0, 0, 0, \
		V4L2_DV_FL_CAN_REDUCE_FPS | V4L2_DV_FL_IS_CE_VIDEO | \
		V4L2_DV_FL_HAS_CEA861_VIC,) \
}
#endif /* #if LINUX_VERSION_CODE <= KERNEL_VERSION(4, 0, 0) */

#define SUPPORT_INTERLACED 0
static struct sc0710_format formats[] =
{
#if SUPPORT_INTERLACED
	{  858,  262,  720,  240, 1, 2997, 30000, 1001, 8, 0, "720x480i29.97",   V4L2_DV_BT_CEA_720X480I59_94 },
#endif
	{  858,  525,  720,  480, 0, 5994, 60000, 1001, 8, 0, "720x480p59.94",   V4L2_DV_BT_CEA_720X480P59_94 },

#if SUPPORT_INTERLACED
	{  864,  312,  720,  288, 1, 2500, 25000, 1000, 8, 0, "720x576i25",      V4L2_DV_BT_CEA_720X576I50 },
#endif

	{ 1980,  750, 1280,  720, 0, 5000, 50000, 1000, 8, 0, "1280x720p50",     V4L2_DV_BT_CEA_1280X720P50 },
	{ 1650,  750, 1280,  720, 0, 5994, 60000, 1001, 8, 0, "1280x720p59.94",  V4L2_DV_BT_CEA_1280X720P60 },
	{ 1650,  750, 1280,  720, 0, 6000, 60000, 1000, 8, 0, "1280x720p60",     V4L2_DV_BT_CEA_1280X720P60 },

#if SUPPORT_INTERLACED
	{ 2640,  562, 1920,  540, 1, 2500, 25000, 1000, 8, 0, "1920x1080i25",    V4L2_DV_BT_CEA_1920X1080I50 },
	{ 2200,  562, 1920,  540, 1, 2997, 30000, 1001, 8, 0, "1920x1080i29.97", V4L2_DV_BT_CEA_1920X1080I60 },
#endif
	{ 2750, 1125, 1920, 1080, 0, 2400, 24000, 1000, 8, 0, "1920x1080p24",    V4L2_DV_BT_CEA_1920X1080P24 },
	{ 2640, 1125, 1920, 1080, 0, 2500, 25000, 1000, 8, 0, "1920x1080p25",    V4L2_DV_BT_CEA_1920X1080P25 },
	{ 2200, 1125, 1920, 1080, 0, 3000, 30000, 1000, 8, 0, "1920x1080p30",    V4L2_DV_BT_CEA_1920X1080P30 },
	{ 2640, 1125, 1920, 1080, 0, 5000, 50000, 1000, 8, 0, "1920x1080p50",    V4L2_DV_BT_CEA_1920X1080P50 },
	{ 2200, 1125, 1920, 1080, 0, 6000, 60000, 1000, 8, 0, "1920x1080p60",    V4L2_DV_BT_CEA_1920X1080P60 },

	{ 4400, 2250, 3840, 2160, 0, 6000, 60000, 1000, 8, 0, "3840x2160p60",    V4L2_DV_BT_CEA_3840X2160P60 },
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

static int vidioc_s_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s()\n", __func__);

	return -EINVAL; /* No support for setting DV Timings */
}

static int vidioc_g_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(0, "%s()\n", __func__);

	if (dev->fmt == NULL)
		return -EINVAL;

	/* Return the current detected timings. */
	*timings = dev->fmt->dv_timings;

	return 0;
}

static int vidioc_query_dv_timings(struct file *file, void *_fh, struct v4l2_dv_timings *timings)
{
//	struct sc0710_dma_channel *ch = video_drvdata(file);
//	struct sc0710_dev *dev = ch->dev;

	int ret = -ENOMEM;

#if 0
	struct hdpvr_video_info *vid_info;
	bool interlaced;
	int ret = 0;
	int i;

	fh->legacy_mode = false;
	if (dev->options.video_input)
		return -ENODATA;
	vid_info = get_video_info(dev);
	if (vid_info == NULL)
		return -ENOLCK;
	interlaced = vid_info->fps <= 30;
	for (i = 0; i < ARRAY_SIZE(hdpvr_dv_timings); i++) {
		const struct v4l2_bt_timings *bt = &hdpvr_dv_timings[i].bt;
		unsigned hsize;
		unsigned vsize;
		unsigned fps;

		hsize = bt->hfrontporch + bt->hsync + bt->hbackporch + bt->width;
		vsize = bt->vfrontporch + bt->vsync + bt->vbackporch +
			bt->il_vfrontporch + bt->il_vsync + bt->il_vbackporch +
			bt->height;
		fps = (unsigned)bt->pixelclock / (hsize * vsize);
		if (bt->width != vid_info->width ||
		    bt->height != vid_info->height ||
		    bt->interlaced != interlaced ||
		    (fps != vid_info->fps && fps + 1 != vid_info->fps))
			continue;
		*timings = hdpvr_dv_timings[i];
		break;
	}
	if (i == ARRAY_SIZE(hdpvr_dv_timings))
		ret = -ERANGE;
	kfree(vid_info);
#endif
	return ret;
}

/* Enum all possible timings we could support. */
static int vidioc_enum_dv_timings(struct file *file, void *_fh, struct v4l2_enum_dv_timings *timings)
{
//	struct sc0710_dma_channel *ch = video_drvdata(file);
//	struct sc0710_dev *dev = ch->dev;

	memset(timings->reserved, 0, sizeof(timings->reserved));

	if (timings->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	timings->timings = formats[timings->index].dv_timings;

	return 0;
}

static int vidioc_dv_timings_cap(struct file *file, void *_fh, struct v4l2_dv_timings_cap *cap)
{
//	struct sc0710_dma_channel *ch = video_drvdata(file);
//	struct sc0710_dev *dev = ch->dev;

	cap->type = V4L2_DV_BT_656_1120;
	cap->bt.min_width = 720;
	cap->bt.max_width = 1920;
	cap->bt.min_height = 480;
	cap->bt.max_height = 1080;
	cap->bt.min_pixelclock = 27000000;
	cap->bt.max_pixelclock = 74250000;
	cap->bt.standards = V4L2_DV_BT_STD_CEA861;
	cap->bt.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE;
#if SUPPORT_INTERLACED
	cap->bt.capabilities |= V4L2_DV_BT_CAP_INTERLACED;
#endif

	return 0;
}

static struct videobuf_queue *get_queue(struct sc0710_fh *fh)
{
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &fh->vidq;
	default:
		return NULL;
	}
}

static int vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	//struct video_device *vdev = video_devdata(file);

	strcpy(cap->driver, "sc0710");
	strlcpy(cap->card, sc0710_boards[dev->board].name, sizeof(cap->card));
	sprintf(cap->bus_info, "PCIe:%s", pci_name(dev->pci));
	
	cap->capabilities  = V4L2_CAP_READWRITE | V4L2_CAP_STREAMING | V4L2_CAP_AUDIO;
	cap->capabilities |= V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	dprintk(1, "%s()\n", __func__);

	i->index = 0;
	i->type  = V4L2_INPUT_TYPE_CAMERA;
	strcpy(i->name, "HDMI");

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s(%d)\n", __func__, i);

	return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	dprintk(1, "%s()\n", __func__);

	*i = 0;

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	struct sc0710_fh *fh = priv;
	return videobuf_reqbufs(get_queue(fh), p);
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sc0710_fh *fh = priv;
	return videobuf_querybuf(get_queue(fh), p);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sc0710_fh *fh = priv;
	return videobuf_qbuf(get_queue(fh), p);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct sc0710_fh *fh = priv;
	return videobuf_dqbuf(get_queue(fh), p, file->f_flags & O_NONBLOCK);
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sc0710_fh *fh = priv;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;
	int ret;

	dprintk(1, "%s(ch#%d)\n", __func__, ch->nr);

	if (unlikely(fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE))
		return -EINVAL;
	if (unlikely(i != fh->type))
		return -EINVAL;

#if 0
	if (IS_FORMAT_640x480p60(channel))
		channel->frame_format_len = MAX_USER_VIDEO_BUFFER_640x480;

	else if (IS_FORMAT_720x480i30(channel) || IS_FORMAT_720x480p60(channel))
		channel->frame_format_len = MAX_USER_VIDEO_BUFFER_720x480;

	else if (IS_FORMAT_720x576i25(channel) || IS_FORMAT_720x576p50(channel))
		channel->frame_format_len = MAX_USER_VIDEO_BUFFER_720x576;

	else if (IS_FORMAT_1280x720p50(channel) || IS_FORMAT_1280x720p60(channel))
		channel->frame_format_len = MAX_USER_VIDEO_BUFFER_1280;

	else if (IS_FORMAT_1920x1080i25(channel) || IS_FORMAT_1920x1080i30(channel))
		channel->frame_format_len = MAX_USER_VIDEO_BUFFER_1920;

	else
		channel->frame_format_len = 0;
#endif

	if (sc0710_dma_channels_start(dev) < 0)
		return -EINVAL;

	mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);

	ret = videobuf_streamon(get_queue(fh));
#if 0
	if (ret == 0)
		dev->lastStreamonFH = fh;
#endif

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct sc0710_fh *fh = priv;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;
	//struct tm6200_audio_dev *chip = channel->audio_dev;
	int err = 0;

	dprintk(1, "%s()\n", __func__);

	if (fh->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (i != fh->type)
		return -EINVAL;

#if 0
	if (chip && chip->capture_pcm_substream)
		tm6200_capture_disconnect(chip->capture_pcm_substream);
#endif

	del_timer(&ch->timeout);

	sc0710_dma_channels_stop(dev);

	err = videobuf_streamoff(get_queue(fh));

#if 0
	if (err == 0)
		dev->lastStreamonFH = 0;
#endif

	return err;
}

void sc0710_dma_free(struct videobuf_queue *q, struct sc0710_dma_channel *ch, struct sc0710_buffer *buf)
{
	videobuf_waiton(q, &buf->vb, 0, 0);
	videobuf_vmalloc_free(&buf->vb);
	buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

static int sc0710_prepare_buffer(struct videobuf_queue *q, struct sc0710_dma_channel *ch, struct sc0710_buffer *buf, enum v4l2_field field)
{
	struct sc0710_dev *dev = ch->dev;
	const struct sc0710_format *fmt = dev->fmt;
	int rc = 0;

	/* check settings */
	if (fmt == 0)
		return -EINVAL;

	buf->vb.size = fmt->framesize;

	dprintk(2, "%s() Resolution: %dx%d\n", __func__, fmt->width, fmt->height);
	dprintk(2, "%s() vb.width = %d\n", __func__, buf->vb.width);
	dprintk(2, "%s() vb.height = %d\n", __func__, buf->vb.height);
	dprintk(2, "%s() vb.size = %ld\n", __func__, buf->vb.size);
	dprintk(2, "%s() vb.bsize = %lu\n", __func__, buf->vb.bsize);
	dprintk(2, "%s() vb.baddr = %lx\n", __func__, buf->vb.baddr);

	if ((buf->vb.baddr != 0)  && (buf->vb.bsize < buf->vb.size)) {
		return -EINVAL;
	}

	/* alloc + fill struct (if changed) */
	if (buf->vb.width != fmt->width || buf->vb.height != fmt->height || buf->vb.field != field || buf->fmt != fmt)
	{
		buf->vb.width  = fmt->width;
		buf->vb.height = fmt->height;
		buf->vb.field  = field;
		buf->fmt       = fmt;

		sc0710_dma_free(q, ch, buf);
	}

	if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size) {
		return -EINVAL;
	}

#if 0
	if (buf->vb.field == 0)
		buf->vb.field = tm6200_find_field_format(fmt);
#endif

	if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
		buf->vb.width  = fmt->width;
		buf->vb.height = fmt->height;
		buf->vb.field  = field;
		buf->fmt       = fmt;

		if (0 != (rc = videobuf_iolock(q, &buf->vb, 0)))
			goto fail;
	}

	buf->vb.state = VIDEOBUF_PREPARED;
	return 0;

fail:
	sc0710_dma_free(q, ch, buf);
	return rc;

}

/* Inform V4L how large the buffer needs to be in-order to 
 * queue a frame of video.
 */
static int buffer_setup(struct videobuf_queue *q, unsigned int *count, unsigned int *size)
{
	struct sc0710_fh *fh = q->priv_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;

	if (dev->fmt == 0)
		return -ENOMEM;

	*size = dev->fmt->framesize;
	dprintk(2, "%s() buffer size will be %d bytes\n", __func__, *size);

	if (0 == *count)
		*count = 32;

	return 0;
}

static int buffer_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct sc0710_fh *fh = q->priv_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_buffer *buf = container_of(vb, struct sc0710_buffer, vb);

	return sc0710_prepare_buffer(q, ch, buf, field);
}

static void buffer_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct sc0710_fh *fh = q->priv_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_buffer *buf = container_of(vb, struct sc0710_buffer, vb);

	buf->vb.state = VIDEOBUF_QUEUED;
	list_add_tail(&buf->vb.queue, &ch->v4l2_capture_list);
}

static void buffer_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct sc0710_fh *fh = q->priv_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_buffer *buf = container_of(vb, struct sc0710_buffer, vb);

        sc0710_dma_free(q, ch, buf);
}

static struct videobuf_queue_ops sc0710_video_qops =
{
	.buf_setup    = buffer_setup,
	.buf_prepare  = buffer_prepare,
	.buf_queue    = buffer_queue,
	.buf_release  = buffer_release,
};

static int sc0710_video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sc0710_dma_channel *ch = video_drvdata(file);
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_fh *fh;
	enum v4l2_buf_type type = 0;

	switch (vdev->vfl_type) {
	case VFL_TYPE_GRABBER:
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		break;
	}

	dprintk(1, "%s() dev=%s type=%s\n", __func__, video_device_node_name(vdev), v4l2_type_names[type]);

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL)
		return -ENOMEM;

	fh->ch   = ch;
	fh->type = type;
	v4l2_fh_init(&fh->fh, vdev);

	videobuf_queue_vmalloc_init(&fh->vidq, &sc0710_video_qops,
		&dev->pci->dev, &ch->slock,
		V4L2_BUF_TYPE_VIDEO_CAPTURE,
		V4L2_FIELD_INTERLACED,
		sizeof(struct sc0710_buffer),
		fh, NULL);

	init_timer(&ch->timeout);
	ch->timeout.function = sc0710_vid_timeout;
	ch->timeout.data     = (unsigned long)ch;

	file->private_data = fh;

	mutex_lock(&ch->lock);
	ch->videousers++;
	mutex_unlock(&ch->lock);

	return 0;
}

static int sc0710_video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s() dev=%s type=%s\n", __func__, video_device_node_name(vdev), v4l2_type_names[fh->type]);

	mutex_lock(&ch->lock);
	ch->videousers--;
	if (ch->videousers == 0) {
		vidioc_streamoff(file, fh, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	}
	mutex_unlock(&ch->lock);

	videobuf_queue_cancel(&fh->vidq);
	videobuf_mmap_free(&fh->vidq);

	file->private_data = NULL;
	kfree(fh);

	return 0;
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i);
static ssize_t sc0710_video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dma_channel *ch = fh->ch;
	struct sc0710_dev *dev = ch->dev;

	dprintk(2, "%s()\n", __func__);

	if (ch->videousers > 1) {
		dprintk(1, "%s() -EBUSY\n", __func__);
		return -EBUSY;
	}

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		if (sc0710_dma_channel_state(ch) != STATE_RUNNING) {
			vidioc_streamon(file, fh, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		}
		return videobuf_read_one(&fh->vidq, data, count, ppos, file->f_flags & O_NONBLOCK);
	default:
		return -EBUSY;
	}
}

static unsigned int sc0710_video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dev *dev = fh->ch->dev;

	dprintk(1, "%s()\n", __func__);

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return videobuf_poll_stream(file, &fh->vidq, wait);
	default:
		return 0;
	}
}

static int sc0710_video_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dev *dev = fh->ch->dev;

	dprintk(1, "%s()\n", __func__);

	return videobuf_mmap_mapper(get_queue(fh), vma);
}

static const struct v4l2_file_operations video_fops = {
	.owner	        = THIS_MODULE,
	.open           = sc0710_video_open,
	.release        = sc0710_video_release,
	.read           = sc0710_video_read,
	.poll		= sc0710_video_poll,
	.mmap           = sc0710_video_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops =
{
	.vidioc_querycap         = vidioc_querycap,

	.vidioc_s_dv_timings     = vidioc_s_dv_timings,
	.vidioc_g_dv_timings     = vidioc_g_dv_timings,
	.vidioc_query_dv_timings = vidioc_query_dv_timings,
	.vidioc_enum_dv_timings  = vidioc_enum_dv_timings,
	.vidioc_dv_timings_cap   = vidioc_dv_timings_cap,

	.vidioc_enum_input       = vidioc_enum_input,
	.vidioc_g_input          = vidioc_g_input,
	.vidioc_s_input          = vidioc_s_input,

	.vidioc_reqbufs          = vidioc_reqbufs,
	.vidioc_querybuf         = vidioc_querybuf,
	.vidioc_qbuf             = vidioc_qbuf,
	.vidioc_dqbuf            = vidioc_dqbuf,
	.vidioc_streamon         = vidioc_streamon,
	.vidioc_streamoff        = vidioc_streamoff,
};

static struct video_device sc0710_video_template =
{
	.name                 = "sc0710-video",
	.fops                 = &video_fops,
	.ioctl_ops	      = &video_ioctl_ops,
};

static void sc0710_vid_timeout(unsigned long data)
{
	struct sc0710_dma_channel *ch = (struct sc0710_dma_channel *)data;
	struct sc0710_dev *dev = ch->dev;
	struct sc0710_buffer *buf;
	unsigned long flags;
	u8 *dst;

	dprintk(0, "%s(ch#%d)\n", __func__, ch->nr);

	/* Return all of the buffers in error state, so the vbi/vid inode
	 * can return from blocking.
	 */
	spin_lock_irqsave(&ch->v4l2_capture_list_lock, flags);
	while (!list_empty(&ch->v4l2_capture_list)) {
		buf = list_entry(ch->v4l2_capture_list.next, struct sc0710_buffer, vb.queue);

		dst = videobuf_to_vmalloc(&buf->vb);
		if (dst) {
			fill_frame(ch, dst, buf->vb.width, buf->vb.height, FILL_MODE_COLORBARS);
		}

		buf->vb.state = VIDEOBUF_DONE;

		do_gettimeofday(&buf->vb.ts);
		list_del(&buf->vb.queue);
		wake_up(&buf->vb.done);
	}
	spin_unlock_irqrestore(&ch->v4l2_capture_list_lock, flags);

	/* re-set the buffer timeout */
	mod_timer(&ch->timeout, jiffies + VBUF_TIMEOUT);
}

void sc0710_video_unregister(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s()\n", __func__);

	if (ch->v4l_device) {
		if (video_is_registered(ch->v4l_device))
			video_unregister_device(ch->v4l_device);
		else
			video_device_release(ch->v4l_device);

		ch->v4l_device = NULL;
	}
}

int sc0710_video_register(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	int err;

	spin_lock_init(&ch->slock);

	ch->v4l_device = video_device_alloc();
	if (ch->v4l_device == NULL) {
		printk(KERN_INFO "%s: can't init video device\n", dev->name);
		return -1;
	}
	*ch->v4l_device = sc0710_video_template;
	ch->v4l_device->lock = &ch->lock;
	ch->v4l_device->release = video_device_release;
	ch->v4l_device->parent = &dev->pci->dev;
	strcpy(ch->v4l_device->name, "sc0710 video");

	video_set_drvdata(ch->v4l_device, ch);

	err = video_register_device(ch->v4l_device, VFL_TYPE_GRABBER, -1);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n", dev->name);
		return -1;
	}

	printk(KERN_INFO "%s: registered device %s [v4l2]\n",
	       dev->name, video_device_node_name(ch->v4l_device));

	return 0; /* Success */
}

