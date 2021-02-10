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

static int video_debug = 1;

#define dprintk(level, fmt, arg...)\
        do { if (video_debug >= level)\
                printk(KERN_DEBUG "%s: " fmt, dev->name, ## arg);\
        } while (0)

static unsigned int video_nr[] = {[0 ... (SC0710_MAXBOARDS - 1)] = UNSET };

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
	dprintk(1, "%s() returns %d\n", __func__, *i);

	return 0;
}
static const struct v4l2_file_operations video_fops = {
	.owner	        = THIS_MODULE,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.read           = vb2_fop_read,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = vb2_fop_mmap,
};

static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,

	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
};

static struct video_device sc0710_video_template =
{
	.name                 = "sc0710-video",
	.fops                 = &video_fops,
	.ioctl_ops	      = &video_ioctl_ops,
};

static struct video_device *sc0710_video_init(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	struct video_device *vfd;
	dprintk(1, "%s()\n", __func__);

	vfd = video_device_alloc();
	if (NULL == vfd)
		return NULL;

	*vfd          = sc0710_video_template;
	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->release  = video_device_release;

	snprintf(vfd->name, sizeof(vfd->name), "%s (video)", sc0710_boards[dev->board].name);

	video_set_drvdata(vfd, ch);

	return vfd;
}

static void sc0710_vid_timeout(unsigned long data)
{
	struct sc0710_dma_channel *ch = (struct sc0710_dma_channel *)data;
	struct sc0710_dev *dev = ch->dev;
	dprintk(1, "%s()\n", __func__);
}

void sc0710_video_unregister(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;

	dprintk(1, "%s()\n", __func__);

	if (ch->video_dev) {
		if (video_is_registered(ch->video_dev))
			video_unregister_device(ch->video_dev);
		else
			video_device_release(ch->video_dev);

		ch->video_dev = NULL;
	}
}

int sc0710_video_register(struct sc0710_dma_channel *ch)
{
	struct sc0710_dev *dev = ch->dev;
	int err;

#if 0
        err = v4l2_device_register(&dev->pci->dev, &ch->v4l2_dev);
        if (err < 0)
		return -1;
#endif

	INIT_LIST_HEAD(&ch->vidq.active);
	INIT_LIST_HEAD(&ch->vidq.queued);

#if 0
	ch->vidq.timeout.function = sc0710_vid_timeout;
	ch->vidq.timeout.data     = (unsigned long)ch;
	init_timer(&ch->vidq.timeout);
#endif

	ch->video_dev = sc0710_video_init(ch);
	if (ch->video_dev == NULL) {
		printk(KERN_INFO "%s: can't init video device\n", dev->name);
		return -1;
	}

	err = video_register_device(ch->video_dev, VFL_TYPE_GRABBER, video_nr[ch->nr]);
	if (err < 0) {
		printk(KERN_INFO "%s: can't register video device\n", dev->name);
		return -1;
	}

	printk(KERN_INFO "%s: registered device %s [v4l2]\n",
	       dev->name, video_device_node_name(ch->video_dev));

	return 0; /* Success */
}

