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

#include "sc0710.h"

static unsigned int audio_debug = 2;
module_param(audio_debug, int, 0644);
MODULE_PARM_DESC(audio_debug, "enable debug messages [audio]");

#define dprintk(level, fmt, arg...)\
	do { if (audio_debug >= level)\
		printk(KERN_DEBUG "%s/0: " fmt, dev->name, ## arg);\
	} while (0)

int sc0710_audio_deliver_samples(struct sc0710_dev *dev, struct sc0710_dma_channel *ch,
	const u8 *buf, int bitdepth, int strideBytes, int channels, int samplesPerChannel)
{
	struct sc0710_audio_dev *chip;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	int period_elapsed = 0;
	u8 *ptr = (u8 *)buf;
	u8 *dst;
	int i;
	
	if (channels != 2)
		return -1;
	if (bitdepth != 16)
		return -1;
	if (samplesPerChannel <= 0)
		return -1;

	chip = ch->audio_dev;
	if (!chip) {
		printk("%s() audio chip is NULL \n", __func__);
		return -1;
	}

	substream = chip->substream;
	if (!substream) {
		printk("%s() audio capture substream is NULL\n", __func__);
		return -1;
	}

	runtime = substream->runtime;
	if (!runtime) {
		printk("%s() audio capture runtime is NULL\n", __func__);
		return -1;
	}
	if (!runtime->dma_area) {
		printk("%s() audio capture runtime->dma_area is NULL\n", __func__);
		return -1;
	}
	if (!runtime->buffer_size) {
		printk("%s() audio capture runtime->buffer_size is zero\n", __func__);
		return -1;
	}

#if 0
	dprintk(1, "%s() wrote %d samples stride %d\n", __func__, samplesPerChannel, strideBytes);
	for (i = 0; i < 128; i++)
		printk(" %02x", *(ptr + i));
	printk("\n");
	//return 0;
#endif

	/* Hardware is going to give is a series of s16 words in the following format:
	 *    L1  R1  L2  R2  L3  R3  L4  R4
	 *   s16 s16 s16 s16 s16 s16 s16 s16 
	 * Only Pair L1/R1 will be value, the remaining should be ignored.
	 */

	dst = (u8 *)runtime->dma_area + (chip->buffer_ptr * 4);

	for (i = 0; i < samplesPerChannel; i++) {

		/* Make sure we can fit a left and right sample in. */
		if (chip->buffer_ptr == runtime->buffer_size) {
			dst = (u8 *)runtime->dma_area;
			chip->buffer_ptr = 0;
		} else
		if (chip->buffer_ptr + 1 > runtime->buffer_size) {
			printk("%s() overflow\n", __func__);
			return -1;
		}

		/* TODO: Do this in dwords, its faster. */
		*(dst++) = *(ptr + 0);
		*(dst++) = *(ptr + 1);
		*(dst++) = *(ptr + 2);
		*(dst++) = *(ptr + 3);

#if 0
		if (i < 5) {
			dprintk(0, "data: %02x%02x %02x%02x\n", *(ptr + 0), *(ptr + 1), *(ptr + 2), *(ptr + 3));
		}
#endif

		sc0710_things_per_second_update(&ch->audioSamplesPerSecond, 2);

		ptr += strideBytes;
		chip->buffer_ptr++;
	}

	snd_pcm_stream_lock(substream);
	snd_pcm_stream_unlock(substream);

#if 0
	if (chip->period_pos >= runtime->period_size) {
		chip->period_pos -= runtime->period_size;
		period_elapsed = 1;
	}
	//if (period_elapsed)
#endif

	snd_pcm_period_elapsed(substream);

	dprintk(1, "%s() wrote %d samples, period elapsed %d rt buf size: %d, %d\n", __func__, samplesPerChannel, period_elapsed,
		runtime->buffer_size,
		chip->buffer_ptr);

	return 0; /* Success */
}

static struct snd_pcm_hardware snd_sc0710_hw_capture =
{
	.info = SNDRV_PCM_INFO_BLOCK_TRANSFER |
	    SNDRV_PCM_INFO_MMAP |
	    SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP_VALID,
	.formats          = SNDRV_PCM_FMTBIT_S16_LE,
	.rates            = SNDRV_PCM_RATE_48000,
	.rate_min         = 48000,
	.rate_max         = 48000,
	.channels_min     = 2,
	.channels_max     = 2,
	.buffer_bytes_max = 32768,
	.period_bytes_min = 4096,
	.period_bytes_max = 32768,
	.periods_min      = 1,
	.periods_max      = 1024,
};

static int snd_sc0710_capture_open(struct snd_pcm_substream *substream)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sc0710_dev *dev = chip->dev;
	struct sc0710_dma_channel *ch = &dev->channel[1];

	dprintk(1, "%s()\n", __func__);

	if (!chip) {
		printk(KERN_ERR "%s() No chip\n", __func__);
		return -ENODEV;
	}
	if (!ch) {
		printk(KERN_ERR "%s() No channel\n", __func__);
		return -ENODEV;
	}

	chip->substream = substream;

	runtime->private_data = chip;
	runtime->hw = snd_sc0710_hw_capture;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);

	return 0;
}

static int snd_sc0710_pcm_close(struct snd_pcm_substream *substream)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct sc0710_dev *dev = chip->dev;

	dprintk(1, "%s()\n", __func__);

	/* Stop the hardware */

	return 0;
}

static int snd_sc0710_hw_capture_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sc0710_dev *dev = chip->dev;
	int size;

	size = params_buffer_bytes(hw_params);
	dprintk(1, "%s() buffer_bytes %d\n", __func__, size);

	if (runtime->dma_area) {
		if (runtime->dma_bytes > size)
			return 0;
		kfree(runtime->dma_area);
	}
	runtime->dma_area = kzalloc(size, GFP_KERNEL);
	if (!runtime->dma_area)
		return -ENOMEM;
	else
		runtime->dma_bytes = size;

	return 0; /* Success */
}

static int snd_sc0710_hw_capture_free(struct snd_pcm_substream *substream)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct sc0710_dev *dev = chip->dev;
	//struct sc0710_dma_channel *ch = &dev->channel[1];

	dprintk(1, "%s() rate = %d\n", __func__, substream->runtime->rate);

	/* Stop the stream */

	return 0;
}

static int snd_sc0710_prepare(struct snd_pcm_substream *substream)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct sc0710_dev *dev = chip->dev;
	//struct sc0710_dma_channel *ch = &dev->channel[1];

	dprintk(1, "%s() requested rate = %d\n", __func__, substream->runtime->rate);

	chip->buffer_ptr = 0;

	if (substream->runtime->rate != 48000) {
		dprintk(1, "%s() audio rate mismatch (%u vs %u)\n", __func__, substream->runtime->rate, 48000);
		return -EINVAL;
	}

	/* Configure the h/w for out audio requirements */

	return 0;
}

static int snd_sc0710_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	struct sc0710_dev *dev = chip->dev;

	dprintk(1, "%s() cmd %d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* Start h/w */
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
		/* Stop h/w */
		return 0;

	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t snd_sc0710_capture_pointer(struct snd_pcm_substream
						    *substream)
{
	struct sc0710_audio_dev *chip = snd_pcm_substream_chip(substream);
	//printk("%s()\n", __func__);
	return chip->buffer_ptr;
}

static struct page *snd_pcm_pd_get_page(struct snd_pcm_substream *subs,
					unsigned long offset)
{
	void *pageptr = subs->runtime->dma_area + offset;
	printk("%s()\n", __func__);
	return vmalloc_to_page(pageptr);
}

static struct snd_pcm_ops pcm_capture_ops =
{
	.open      = snd_sc0710_capture_open,
	.close     = snd_sc0710_pcm_close,
	.ioctl     = snd_pcm_lib_ioctl,
	.hw_params = snd_sc0710_hw_capture_params,
	.hw_free   = snd_sc0710_hw_capture_free,
	.prepare   = snd_sc0710_prepare,
	.trigger   = snd_sc0710_capture_trigger,
	.pointer   = snd_sc0710_capture_pointer,
	.page      = snd_pcm_pd_get_page,
};

void sc0710_audio_unregister(struct sc0710_dev *dev)
{
	struct sc0710_dma_channel *channel = &dev->channel[1];
	struct sc0710_audio_dev *chip = channel->audio_dev;

	dprintk(1, "%s()\n", __func__);
	dprintk(0, "Unregistered ALSA audio device %p\n", chip);

	if (!chip) {
		printk(KERN_ERR "%s() no chip!\n", __func__);
		return;
	}

	snd_card_free(chip->card);
}

/*
 * create a PCM device
 */
static int snd_sc0710_pcm(struct sc0710_audio_dev *chip, int device, char *name)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, name, device, 0, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = chip;

	strcpy(pcm->name, name);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &pcm_capture_ops);

	return 0;
}

int sc0710_audio_register(struct sc0710_dev *dev)
{
	struct snd_card *card;
	struct sc0710_audio_dev *chip;

	/* We register the audio device using DMA channel #2 but we
	 * switch the DMA channel when the user selects a different
	 * video input.
	 */
	struct sc0710_dma_channel *channel = &dev->channel[1];
	int err;

	err = snd_card_new(&dev->pci->dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			      THIS_MODULE, sizeof(struct sc0710_audio_dev),
			      &card);
	if (err < 0)
		goto error;

	chip = (struct sc0710_audio_dev *)card->private_data;
	chip->card = card;
	chip->dev = dev;
	chip->buffer_ptr = 0;

	err = snd_sc0710_pcm(chip, 0, "sc0710 HDMI");
	if (err < 0)
		goto error;

	strcpy(card->driver, "sc0710");
	sprintf(card->shortname, "Elgato (Yuan sc0710)");
	sprintf(card->longname, "%s at %s", card->shortname, dev->name);
	strcpy(card->mixername, "sc0710");

	err = snd_card_register(card);
	if (err < 0)
		goto error;

	channel->audio_dev = chip;
	dev->channel[1].audio_dev = chip;

	snd_card_set_dev(card, &dev->pci->dev);

	dprintk(0, "Registered ALSA audio device %p card %p\n",
		channel->audio_dev, channel->audio_dev->card);
	return 0;

error:
	snd_card_free(card);
	printk(KERN_ERR "%s(): Failed to register analog "
	       "audio adapter\n", __func__);

	return 0;
}
