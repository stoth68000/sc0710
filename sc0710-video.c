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
	dprintk(1, "%s() returns %d\n", __func__, *i);

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

	dprintk(1, "%s()\n", __func__);

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

	buf->vb.size = (fmt->width * fmt->height * (fmt->depth / 8));

	dprintk(3, "%s() Resolution: %dx%d\n", __func__, fmt->width, fmt->height);
	dprintk(3, "%s() vb.width = %d\n", __func__, buf->vb.width);
	dprintk(3, "%s() vb.height = %d\n", __func__, buf->vb.height);
	dprintk(3, "%s() vb.size = %ld\n", __func__, buf->vb.size);
	dprintk(3, "%s() vb.bsize = %lu\n", __func__, buf->vb.bsize);
	dprintk(3, "%s() vb.baddr = %lx\n", __func__, buf->vb.baddr);

	if ((buf->vb.baddr != 0)  && (buf->vb.bsize < buf->vb.size)) {
		return -EINVAL;
	}

	/* alloc + fill struct (if changed) */
	if (buf->vb.width != fmt->width || buf->vb.height != fmt->height ||
	    buf->vb.field != field || buf->fmt != fmt)
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
        //struct sc0710_fh *fh = q->priv_data;

#if 0
        *size = fh->fmt->depth*fh->width*fh->height >> 3;
        if (0 == *count)
                *count = 32;

        if (*size * *count > vid_limit * 1024 * 1024)
                *count = (vid_limit * 1024 * 1024) / *size;
#endif

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

	dprintk(1, "%s()\n", __func__);

	if ((ch->videousers > 1) || (ch->state == STATE_RUNNING)) {
		dprintk(1, "%s() -EBUSY\n", __func__);
		return -EBUSY;
	}

	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		vidioc_streamon(file, fh, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		return videobuf_read_one(&fh->vidq, data, count, ppos, file->f_flags & O_NONBLOCK);
	default:
		return -EBUSY;
	}
}

static unsigned int sc0710_video_poll(struct file *file, struct poll_table_struct *wait)
{
	struct sc0710_fh *fh = file->private_data;
	struct sc0710_dev *dev = fh->ch->dev;

	dprintk(3, "%s()\n", __func__);

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
#if 0
	.read           = sc0710_video_read,
	.poll		= sc0710_video_poll,
	.mmap           = sc0710_video_mmap,
#endif
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops video_ioctl_ops =
{
	.vidioc_querycap      = vidioc_querycap,

	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,

	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
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
	dprintk(1, "%s()\n", __func__);
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
	INIT_LIST_HEAD(&ch->vidq.active);
	INIT_LIST_HEAD(&ch->vidq.queued);

	ch->vidq.timeout.function = sc0710_vid_timeout;
	ch->vidq.timeout.data     = (unsigned long)ch;
	init_timer(&ch->vidq.timeout);

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

