// SPDX-License-Identifier: GPL-2.0-only

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include "gc555.h"

#define GC555_AUDIO_BUFFER_FRAMES(rate_hz)	((rate_hz) / 100U)
#define GC555_AUDIO_DMA_BYTES(rate_hz, channels) \
	(GC555_AUDIO_BUFFER_FRAMES(rate_hz) * (channels) * sizeof(__le16))
#define GC555_AUDIO_MIN_PERIOD_BYTES \
	GC555_AUDIO_DMA_BYTES(GC555_AUDIO_RATE_32000_HZ, \
			      GC555_AUDIO_CHANNELS_STEREO)
#define GC555_AUDIO_MAX_DMA_BYTES \
	GC555_AUDIO_DMA_BYTES(GC555_AUDIO_RATE_48000_HZ, \
			      GC555_AUDIO_CHANNELS_7_1)
#define GC555_AUDIO_MAX_PERIOD_BYTES	(GC555_AUDIO_MAX_DMA_BYTES * 32U)
#define GC555_AUDIO_MAX_BUFFER_BYTES	(GC555_AUDIO_MAX_DMA_BYTES * 64U)

struct gc555_audio {
	struct gc555_dev *gc555;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	struct mutex control_lock;
	spinlock_t state_lock;
	snd_pcm_uframes_t hw_ptr;
	snd_pcm_uframes_t period_progress;
	bool capture_enabled;
	bool dma_running;
	bool disconnected;
};

static const struct snd_pcm_hardware gc555_audio_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
		 SNDRV_PCM_RATE_48000,
	.rate_min = GC555_AUDIO_RATE_32000_HZ,
	.rate_max = GC555_AUDIO_RATE_48000_HZ,
	.channels_min = GC555_AUDIO_CHANNELS_STEREO,
	.channels_max = GC555_AUDIO_CHANNELS_7_1,
	.buffer_bytes_max = GC555_AUDIO_MAX_BUFFER_BYTES,
	.period_bytes_min = GC555_AUDIO_MIN_PERIOD_BYTES,
	.period_bytes_max = GC555_AUDIO_MAX_PERIOD_BYTES,
	.periods_min = 2,
	.periods_max = 64,
};

static const unsigned int gc555_audio_channel_counts[] = {
	GC555_AUDIO_CHANNELS_STEREO,
	GC555_AUDIO_CHANNELS_7_1,
};

static const struct snd_pcm_hw_constraint_list gc555_audio_channel_list = {
	.count = ARRAY_SIZE(gc555_audio_channel_counts),
	.list = gc555_audio_channel_counts,
};

static const u8 gc555_audio_7_1_alsa_from_hdmi[] = {
	/* HDMI places FC/LFE after the surround pairs; ALSA does not. */
	0, 1, 2, 3, 6, 7, 4, 5,
};

static bool gc555_audio_is_disconnected(struct gc555_audio *audio)
{
	unsigned long flags;
	bool disconnected;

	spin_lock_irqsave(&audio->state_lock, flags);
	disconnected = audio->disconnected;
	spin_unlock_irqrestore(&audio->state_lock, flags);

	return disconnected;
}

static void gc555_audio_set_disconnected(struct gc555_audio *audio)
{
	unsigned long flags;

	spin_lock_irqsave(&audio->state_lock, flags);
	audio->disconnected = true;
	audio->capture_enabled = false;
	spin_unlock_irqrestore(&audio->state_lock, flags);
}

static int gc555_audio_set_capture_enabled(struct gc555_audio *audio,
					    bool enabled)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&audio->state_lock, flags);
	if (enabled && (audio->disconnected || !audio->dma_running))
		ret = -ENODEV;
	else
		audio->capture_enabled = enabled;
	spin_unlock_irqrestore(&audio->state_lock, flags);

	return ret;
}

static void gc555_audio_copy_frames(struct snd_pcm_runtime *runtime,
				    void *destination, const void *source,
				    snd_pcm_uframes_t frames)
{
	const __le16 *source_samples = source;
	__le16 *destination_samples = destination;
	snd_pcm_uframes_t frame;
	unsigned int channel;

	if (runtime->channels != GC555_AUDIO_CHANNELS_7_1) {
		memcpy(destination, source, frames_to_bytes(runtime, frames));
		return;
	}

	for (frame = 0; frame < frames; frame++) {
		for (channel = 0; channel < GC555_AUDIO_CHANNELS_7_1;
		     channel++)
			destination_samples[frame * GC555_AUDIO_CHANNELS_7_1 +
					    channel] =
				source_samples[frame * GC555_AUDIO_CHANNELS_7_1 +
					       gc555_audio_7_1_alsa_from_hdmi[channel]];
	}
}

static void gc555_audio_receive(void *context, const void *data, size_t bytes)
{
	struct gc555_audio *audio = context;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	snd_pcm_uframes_t first_frames;
	snd_pcm_uframes_t frames;
	snd_pcm_uframes_t old_ptr;
	unsigned long flags;
	unsigned int elapsed = 0;

	if (!audio || !data || !bytes)
		return;

	spin_lock_irqsave(&audio->state_lock, flags);
	substream = audio->substream;
	if (!audio->capture_enabled || !substream) {
		spin_unlock_irqrestore(&audio->state_lock, flags);
		return;
	}

	runtime = substream->runtime;
	if (!runtime || !runtime->dma_area || !runtime->buffer_size ||
	    !runtime->period_size) {
		spin_unlock_irqrestore(&audio->state_lock, flags);
		return;
	}

	frames = bytes_to_frames(runtime, bytes);
	if (!frames || frames > runtime->buffer_size) {
		spin_unlock_irqrestore(&audio->state_lock, flags);
		return;
	}

	old_ptr = audio->hw_ptr;
	first_frames = min(frames, runtime->buffer_size - old_ptr);
	gc555_audio_copy_frames(
		runtime, runtime->dma_area + frames_to_bytes(runtime, old_ptr),
		data, first_frames);
	if (first_frames < frames)
		gc555_audio_copy_frames(
			runtime, runtime->dma_area,
			(const u8 *)data + frames_to_bytes(runtime, first_frames),
			frames - first_frames);

	audio->hw_ptr = (old_ptr + frames) % runtime->buffer_size;
	audio->period_progress += frames;
	while (audio->period_progress >= runtime->period_size) {
		audio->period_progress -= runtime->period_size;
		elapsed++;
	}
	spin_unlock_irqrestore(&audio->state_lock, flags);

	while (elapsed--)
		snd_pcm_period_elapsed(substream);
}

static void gc555_audio_stop_sync(struct gc555_audio *audio)
{
	unsigned long flags;
	bool running;

	gc555_audio_set_capture_enabled(audio, false);

	mutex_lock(&audio->control_lock);
	spin_lock_irqsave(&audio->state_lock, flags);
	running = audio->dma_running;
	audio->dma_running = false;
	spin_unlock_irqrestore(&audio->state_lock, flags);
	if (running)
		gc555_dma_stop_audio(audio->gc555, audio);
	mutex_unlock(&audio->control_lock);
}

static int gc555_audio_pcm_open(struct snd_pcm_substream *substream)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&audio->state_lock, flags);
	if (audio->disconnected)
		ret = -ENODEV;
	else if (audio->substream)
		ret = -EBUSY;
	else {
		audio->substream = substream;
		ret = 0;
	}
	spin_unlock_irqrestore(&audio->state_lock, flags);
	if (ret)
		return ret;

	substream->runtime->hw = gc555_audio_hardware;
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 &gc555_audio_channel_list);
	if (ret < 0)
		goto fail;
	ret = snd_pcm_hw_constraint_integer(substream->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto fail;
	return 0;

fail:
	spin_lock_irqsave(&audio->state_lock, flags);
	if (audio->substream == substream)
		audio->substream = NULL;
	spin_unlock_irqrestore(&audio->state_lock, flags);
	return ret;
}

static int gc555_audio_pcm_close(struct snd_pcm_substream *substream)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);
	unsigned long flags;

	gc555_audio_stop_sync(audio);
	spin_lock_irqsave(&audio->state_lock, flags);
	if (audio->substream == substream)
		audio->substream = NULL;
	spin_unlock_irqrestore(&audio->state_lock, flags);

	return 0;
}

static int gc555_audio_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);
	unsigned long flags;
	bool running;
	int ret = 0;

	spin_lock_irqsave(&audio->state_lock, flags);
	audio->capture_enabled = false;
	audio->hw_ptr = 0;
	audio->period_progress = 0;
	spin_unlock_irqrestore(&audio->state_lock, flags);

	/* Prepare primes DMA; trigger controls whether samples reach ALSA. */
	mutex_lock(&audio->control_lock);
	spin_lock_irqsave(&audio->state_lock, flags);
	running = audio->dma_running;
	spin_unlock_irqrestore(&audio->state_lock, flags);
	if (gc555_audio_is_disconnected(audio)) {
		ret = -ENODEV;
	} else if (!running) {
		ret = gc555_dma_start_audio(audio->gc555,
					     substream->runtime->rate,
					     substream->runtime->channels,
					     gc555_audio_receive, audio);
		if (!ret) {
			spin_lock_irqsave(&audio->state_lock, flags);
			audio->dma_running = true;
			spin_unlock_irqrestore(&audio->state_lock, flags);
		}
	}
	mutex_unlock(&audio->control_lock);

	return ret;
}

static int gc555_audio_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);

	gc555_audio_stop_sync(audio);
	return 0;
}

static int gc555_audio_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);
	bool start;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		start = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		start = false;
		break;
	default:
		return -EINVAL;
	}

	return gc555_audio_set_capture_enabled(audio, start);
}

static snd_pcm_uframes_t
gc555_audio_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct gc555_audio *audio = snd_pcm_substream_chip(substream);
	unsigned long flags;
	snd_pcm_uframes_t pointer;

	spin_lock_irqsave(&audio->state_lock, flags);
	pointer = audio->hw_ptr;
	spin_unlock_irqrestore(&audio->state_lock, flags);

	return pointer;
}

static const struct snd_pcm_ops gc555_audio_pcm_ops = {
	.open = gc555_audio_pcm_open,
	.close = gc555_audio_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_free = gc555_audio_pcm_hw_free,
	.prepare = gc555_audio_pcm_prepare,
	.trigger = gc555_audio_pcm_trigger,
	.pointer = gc555_audio_pcm_pointer,
};

int gc555_audio_init(struct gc555_dev *gc555)
{
	struct gc555_audio *audio;
	struct snd_card *card;
	int ret;

	if (!gc555 || !gc555->dma)
		return -EINVAL;
	if (gc555->audio)
		return 0;

	ret = snd_card_new(gc555->dev, -1, NULL, THIS_MODULE,
			   sizeof(*audio), &card);
	if (ret < 0)
		return dev_err_probe(gc555->dev, ret,
				     "failed to allocate ALSA card\n");

	audio = card->private_data;
	audio->gc555 = gc555;
	audio->card = card;
	mutex_init(&audio->control_lock);
	spin_lock_init(&audio->state_lock);

	strscpy(card->driver, "GC555", sizeof(card->driver));
	strscpy(card->shortname, "AVerMedia Live Gamer BOLT",
		sizeof(card->shortname));
	strscpy(card->longname, "AVerMedia Live Gamer BOLT HDMI Capture",
		sizeof(card->longname));

	ret = snd_pcm_new(card, "GC555 HDMI Capture", 0, 0, 1,
			  &audio->pcm);
	if (ret < 0)
		goto free_card;

	audio->pcm->private_data = audio;
	strscpy(audio->pcm->name, "GC555 HDMI Capture",
		sizeof(audio->pcm->name));
	snd_pcm_set_ops(audio->pcm, SNDRV_PCM_STREAM_CAPTURE,
			&gc555_audio_pcm_ops);

	ret = snd_pcm_set_managed_buffer_all(audio->pcm, SNDRV_DMA_TYPE_VMALLOC,
					     NULL, 0,
					     GC555_AUDIO_MAX_BUFFER_BYTES);
	if (ret < 0)
		goto free_card;

	ret = snd_card_register(card);
	if (ret < 0)
		goto free_card;

	gc555->audio = audio;
	return 0;

free_card:
	snd_card_free(card);
	return dev_err_probe(gc555->dev, ret,
			     "failed to register ALSA capture device\n");
}

void gc555_audio_cleanup(struct gc555_dev *gc555)
{
	struct gc555_audio *audio;
	struct snd_card *card;

	if (!gc555 || !gc555->audio)
		return;

	audio = gc555->audio;
	card = audio->card;
	gc555_audio_set_disconnected(audio);
	snd_card_disconnect(card);
	gc555_audio_stop_sync(audio);
	snd_card_disconnect_sync(card);
	gc555->audio = NULL;
	snd_card_free_when_closed(card);
}

void gc555_audio_suspend(struct gc555_dev *gc555)
{
	struct gc555_audio *audio;

	if (!gc555 || !gc555->audio)
		return;

	audio = gc555->audio;
	snd_pcm_suspend_all(audio->pcm);
	gc555_audio_stop_sync(audio);
	snd_power_change_state(audio->card, SNDRV_CTL_POWER_D3hot);
}

void gc555_audio_resume(struct gc555_dev *gc555)
{
	if (!gc555 || !gc555->audio ||
	    gc555_audio_is_disconnected(gc555->audio))
		return;

	snd_power_change_state(gc555->audio->card, SNDRV_CTL_POWER_D0);
}
