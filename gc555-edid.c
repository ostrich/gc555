// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "gc555.h"

#define GC555_EDID_BLOCK_SIZE	128
#define GC555_EDID_SIZE		256
#define GC555_CTA_MAX_VICS	31
#define GC555_CTA_SCRATCH_SIZE	32

struct gc555_cta_block {
	const u8 *data;
	u8 length;
};

struct gc555_cta_view {
	struct gc555_cta_block audio;
	struct gc555_cta_block video;
	struct gc555_cta_block speaker;
	struct gc555_cta_block hdmi_vsdb;
	struct gc555_cta_block hf_vsdb;
	struct gc555_cta_block video_capability;
	struct gc555_cta_block colorimetry;
	struct gc555_cta_block hdr_static;
	struct gc555_cta_block y420_only;
	struct gc555_cta_block y420_map;
	const u8 *dtds;
	u8 dtd_count;
	u8 flags;
};

static const u8 gc555_edid[GC555_EDID_SIZE] = {
	0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x06, 0xd8, 0x44, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x22, 0x1d, 0x01, 0x03, 0x80, 0xa0, 0x5a, 0x78,
	0xea, 0x08, 0xa5, 0xa2, 0x57, 0x4f, 0xa2, 0x28, 0x0f, 0x50, 0x54, 0x25,
	0x0b, 0x00, 0xd1, 0xc0, 0x81, 0x40, 0x81, 0x80, 0x81, 0x00, 0x8b, 0xc0,
	0x95, 0x00, 0xb3, 0x00, 0x81, 0xc0, 0x08, 0xe8, 0x00, 0x30, 0xf2, 0x70,
	0x5a, 0x80, 0xb0, 0x58, 0x8a, 0x00, 0x6d, 0x55, 0x21, 0x00, 0x00, 0x1e,
	0x0c, 0xdf, 0x80, 0xa0, 0x70, 0x38, 0x40, 0x40, 0x30, 0x40, 0x35, 0x00,
	0x20, 0x2f, 0x21, 0x00, 0x00, 0x1e, 0xe4, 0xac, 0x00, 0xa0, 0xa0, 0x38,
	0x32, 0x40, 0x30, 0x30, 0xca, 0x00, 0x1e, 0x4e, 0x31, 0x00, 0x00, 0x1e,
	0x00, 0x00, 0x00, 0xfc, 0x00, 0x41, 0x56, 0x54, 0x20, 0x47, 0x43, 0x35,
	0x35, 0x35, 0x0a, 0x20, 0x20, 0x20, 0x01, 0x2c, 0x02, 0x03, 0x37, 0xf1,
	0x4c, 0x61, 0x60, 0x5f, 0x5e, 0x5d, 0x1f, 0x5a, 0x3f, 0x13, 0x22, 0x21,
	0x20, 0x23, 0x0f, 0x07, 0x07, 0x83, 0x4f, 0x00, 0x00, 0x67, 0x03, 0x0c,
	0x00, 0x10, 0x00, 0x38, 0x3c, 0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x78, 0x80,
	0x03, 0xe2, 0x00, 0xcf, 0xe3, 0x05, 0xc0, 0x00, 0xe2, 0x0f, 0x03, 0xe3,
	0x06, 0x05, 0x01, 0x56, 0x5e, 0x00, 0xa0, 0xa0, 0xa0, 0x29, 0x50, 0x30,
	0x20, 0x35, 0x00, 0x55, 0x50, 0x21, 0x00, 0x00, 0x1e, 0x6f, 0xc2, 0x00,
	0xa0, 0xa0, 0xa0, 0x55, 0x50, 0x30, 0x20, 0x35, 0x00, 0x55, 0x50, 0x21,
	0x00, 0x00, 0x1e, 0x9e, 0xe8, 0x00, 0x78, 0xa0, 0xa0, 0x67, 0x50, 0x08,
	0x20, 0x98, 0x04, 0x55, 0x50, 0x21, 0x00, 0x00, 0x1e, 0xfc, 0x7e, 0x80,
	0x88, 0x70, 0x38, 0x12, 0x40, 0x18, 0x20, 0x35, 0x00, 0x20, 0x2f, 0x21,
	0x00, 0x00, 0x1e, 0x31,
};

static bool gc555_edid_block_valid(const u8 *block)
{
	u8 checksum = 0;
	unsigned int i;

	for (i = 0; i < GC555_EDID_BLOCK_SIZE; i++)
		checksum += block[i];

	return !checksum;
}

static bool gc555_edid_valid(const u8 *edid, size_t size)
{
	static const u8 header[] = { 0x00, 0xff, 0xff, 0xff,
				     0xff, 0xff, 0xff, 0x00 };
	size_t block_count;
	size_t block;

	if (!edid || size < GC555_EDID_BLOCK_SIZE ||
	    size % GC555_EDID_BLOCK_SIZE)
		return false;
	if (memcmp(edid, header, sizeof(header)))
		return false;

	block_count = size / GC555_EDID_BLOCK_SIZE;
	if (edid[126] + 1 != block_count)
		return false;
	for (block = 0; block < block_count; block++) {
		if (!gc555_edid_block_valid(edid + block * GC555_EDID_BLOCK_SIZE))
			return false;
	}

	return true;
}

static void gc555_cta_set_block(struct gc555_cta_block *reference,
				const u8 *data, u8 length)
{
	if (!reference->data) {
		reference->data = data;
		reference->length = length;
	}
}

static bool gc555_cta_parse(const u8 *cta, struct gc555_cta_view *view)
{
	unsigned int dtd_offset;
	unsigned int offset;

	if (!cta || !view || cta[0] != 0x02 || cta[1] != 0x03 ||
	    !gc555_edid_block_valid(cta))
		return false;

	memset(view, 0, sizeof(*view));
	dtd_offset = cta[2];
	if (dtd_offset < 4 || dtd_offset > 127)
		return false;
	view->flags = cta[3];

	for (offset = 4; offset < dtd_offset;) {
		const u8 *block = cta + offset;
		unsigned int length = block[0] & 0x1f;
		unsigned int tag = block[0] >> 5;
		unsigned int next = offset + length + 1;

		if (next > dtd_offset)
			return false;
		switch (tag) {
		case 1:
			gc555_cta_set_block(&view->audio, block, length);
			break;
		case 2:
			gc555_cta_set_block(&view->video, block, length);
			break;
		case 3:
			if (length >= 3 && block[1] == 0x03 &&
			    block[2] == 0x0c && block[3] == 0x00)
				gc555_cta_set_block(&view->hdmi_vsdb, block,
						    length);
			else if (length >= 3 && block[1] == 0xd8 &&
				 block[2] == 0x5d && block[3] == 0xc4)
				gc555_cta_set_block(&view->hf_vsdb, block,
						    length);
			break;
		case 4:
			gc555_cta_set_block(&view->speaker, block, length);
			break;
		case 7:
			if (!length)
				break;
			switch (block[1]) {
			case 0x00:
				gc555_cta_set_block(&view->video_capability,
						    block, length);
				break;
			case 0x05:
				gc555_cta_set_block(&view->colorimetry, block,
						    length);
				break;
			case 0x06:
				gc555_cta_set_block(&view->hdr_static, block,
						    length);
				break;
			case 0x0e:
				gc555_cta_set_block(&view->y420_only, block,
						    length);
				break;
			case 0x0f:
				gc555_cta_set_block(&view->y420_map, block,
						    length);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		offset = next;
	}

	view->dtds = cta + dtd_offset;
	while (dtd_offset + 18 <= 127 && view->dtd_count < 4) {
		bool nonzero = false;
		unsigned int i;

		for (i = 0; i < 18; i++)
			nonzero |= cta[dtd_offset + i] != 0;
		if (!nonzero)
			break;
		view->dtd_count++;
		dtd_offset += 18;
	}

	return true;
}

/* Keep passthrough modes within receiver and capture timing support. */
static bool gc555_cta_vic_allowed(u8 vic)
{
	switch (vic & 0x7f) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 31:
	case 32:
	case 33:
	case 34:
	case 63:
	case 90:
	case 93:
	case 94:
	case 95:
	case 96:
	case 97:
		return true;
	default:
		return false;
	}
}

static void
gc555_edid_parse_video_caps(const struct gc555_cta_block *video,
			    struct gc555_edid_caps *caps)
{
	unsigned int i;

	if (!video->data)
		return;

	for (i = 1; i <= video->length; i++) {
		switch (video->data[i] & 0x7f) {
		case 16:
			caps->supports_1080p = true;
			break;
		case 94:
		case 95:
			caps->supports_4k30 = true;
			break;
		case 96:
		case 97:
		case 101:
		case 102:
			caps->supports_4k60 = true;
			break;
		default:
			break;
		}
	}
}

static bool gc555_hdmi_vsdb_has_4k_vic(const struct gc555_cta_block *vsdb)
{
	unsigned int latency_bytes = 0;
	unsigned int lengths_offset;
	unsigned int size;

	if (!vsdb->data)
		return false;
	size = vsdb->length + 1;
	if (size <= 8 || !(vsdb->data[8] & BIT(5)))
		return false;
	if (vsdb->data[8] & BIT(7))
		latency_bytes += 2;
	if (vsdb->data[8] & BIT(6))
		latency_bytes += 2;
	lengths_offset = 10 + latency_bytes;

	return lengths_offset < size && vsdb->data[lengths_offset] >> 5;
}

static void
gc555_edid_parse_hdmi_caps(const struct gc555_cta_view *view,
			   struct gc555_edid_caps *caps)
{
	const struct gc555_cta_block *hdmi = &view->hdmi_vsdb;
	const struct gc555_cta_block *forum = &view->hf_vsdb;
	u32 clock_khz;

	if (hdmi->data) {
		caps->hdmi = true;
		if (hdmi->length >= 6) {
			caps->deep_color_30 |= hdmi->data[6] & BIT(4);
			caps->deep_color_36 |= hdmi->data[6] & BIT(5);
			caps->deep_color_ycbcr444 |= hdmi->data[6] & BIT(3);
		}
		if (hdmi->length >= 7) {
			clock_khz = hdmi->data[7] * 5000U;
			caps->max_tmds_clock_khz =
				max(caps->max_tmds_clock_khz, clock_khz);
		}
		caps->supports_4k30 |= gc555_hdmi_vsdb_has_4k_vic(hdmi);
	}

	if (!forum->data)
		return;
	caps->hdmi = true;
	caps->supports_4k60 = true;
	if (forum->length >= 5) {
		clock_khz = forum->data[5] * 5000U;
		caps->max_tmds_clock_khz =
			max(caps->max_tmds_clock_khz, clock_khz);
	}
	if (forum->length >= 6) {
		caps->scdc |= forum->data[6] & BIT(7);
		caps->low_rate_scrambling |= forum->data[6] & BIT(3);
	}
	if (forum->length >= 7) {
		caps->deep_color_ycbcr420_30 |= forum->data[7] & BIT(0);
		caps->deep_color_ycbcr420_36 |= forum->data[7] & BIT(1);
	}
}

static bool gc555_cta_vic_is_4k60(u8 vic)
{
	vic &= 0x7f;

	return vic == 96 || vic == 97 || vic == 101 || vic == 102;
}

static bool gc555_cta_svd_supports_y420(const struct gc555_cta_view *view,
					unsigned int svd)
{
	const struct gc555_cta_block *map = &view->y420_map;
	unsigned int map_byte;

	if (!map->data || !svd || !view->video.data ||
	    svd > view->video.length)
		return false;
	if (map->length == 1)
		return true;

	map_byte = 2 + (svd - 1) / 8;
	if (map_byte > map->length)
		return false;

	return map->data[map_byte] & BIT((svd - 1) % 8);
}

static void
gc555_edid_parse_y420_caps(const struct gc555_cta_view *view,
			   struct gc555_edid_caps *caps)
{
	const struct gc555_cta_block *video = &view->video;
	const struct gc555_cta_block *map = &view->y420_map;
	unsigned int i;

	if (view->y420_only.data) {
		for (i = 2; i <= view->y420_only.length; i++) {
			if (!gc555_cta_vic_is_4k60(view->y420_only.data[i]))
				continue;
			caps->requires_ycbcr420_4k60 = true;
			caps->supports_4k60 = true;
		}
	}

	if (!map->data || !video->data)
		return;
	if (map->length == 1) {
		for (i = 1; i <= video->length; i++)
			caps->supports_ycbcr420_4k60 |=
				gc555_cta_vic_is_4k60(video->data[i]);
		return;
	}

	for (i = 2; i <= map->length; i++) {
		unsigned int bit;

		for (bit = 0; bit < 8; bit++) {
			unsigned int svd = (i - 2) * 8 + bit + 1;

			if (!(map->data[i] & BIT(bit)) || svd > video->length)
				continue;
			caps->supports_ycbcr420_4k60 |=
				gc555_cta_vic_is_4k60(video->data[svd]);
		}
	}
}

int gc555_edid_parse_caps(const u8 *edid, size_t size,
			  struct gc555_edid_caps *caps)
{
	unsigned int block;
	unsigned int i;

	if (!caps)
		return -EINVAL;
	memset(caps, 0, sizeof(*caps));
	if (!gc555_edid_valid(edid, size))
		return -EBADMSG;

	for (i = 0x26; i <= 0x34; i += 2)
		caps->supports_1080p |= edid[i] == 0xd1 && edid[i + 1] == 0xc0;

	for (block = 1; block < size / GC555_EDID_BLOCK_SIZE; block++) {
		struct gc555_cta_view view = {};
		const u8 *extension = edid + block * GC555_EDID_BLOCK_SIZE;

		if (extension[0] != 0x02 || extension[1] != 0x03)
			continue;
		if (!gc555_cta_parse(extension, &view))
			return -EBADMSG;
		gc555_edid_parse_video_caps(&view.video, caps);
		gc555_edid_parse_hdmi_caps(&view, caps);
		gc555_edid_parse_y420_caps(&view, caps);
	}

	return 0;
}

static int gc555_cta_append(u8 *cta, unsigned int *offset,
			    const u8 *data, unsigned int length)
{
	if (!cta || !offset || !data || *offset + length > 127)
		return -E2BIG;

	memcpy(cta + *offset, data, length);
	*offset += length;

	return 0;
}

static bool gc555_cta_find_lpcm(const struct gc555_cta_block *audio,
				u8 sad[3])
{
	unsigned int offset;

	if (!audio || !audio->data)
		return false;
	for (offset = 1; offset + 2 <= audio->length; offset += 3) {
		if (((audio->data[offset] >> 3) & 0x0f) != 1)
			continue;
		memcpy(sad, audio->data + offset, 3);
		return true;
	}

	return false;
}

static void gc555_cta_limit_hdmi_vsdb(u8 *block, unsigned int size)
{
	unsigned int latency_bytes = 0;
	unsigned int video_flags_offset;
	unsigned int lengths_offset;
	unsigned int vic_offset;
	unsigned int vic_count;
	u8 previous_vic = 3;
	unsigned int i;

	if (size > 6)
		block[6] &= 0x7e;
	if (size <= 8)
		return;

	block[8] &= 0xf0;
	if (!(block[8] & BIT(5)))
		return;
	if (block[8] & BIT(7))
		latency_bytes += 2;
	if (block[8] & BIT(6))
		latency_bytes += 2;
	video_flags_offset = 9 + latency_bytes;
	lengths_offset = video_flags_offset + 1;
	if (video_flags_offset >= size)
		return;
	block[video_flags_offset] &= 0x3f;
	if (lengths_offset >= size)
		return;

	vic_count = block[lengths_offset] >> 5;
	vic_offset = lengths_offset + 1;
	for (i = 0; i < vic_count && vic_offset + i < size; i++) {
		if (block[vic_offset + i] == 4)
			block[vic_offset + i] = previous_vic;
		previous_vic = block[vic_offset + i];
	}
}

static void gc555_edid_set_checksum(u8 *block)
{
	u8 sum = 0;
	unsigned int i;

	block[127] = 0;
	for (i = 0; i < 127; i++)
		sum += block[i];
	block[127] = -sum;
}

static int gc555_cta_merge(const u8 *source_cta, const u8 *sink_cta,
			   u8 *output)
{
	struct gc555_cta_view source = {};
	struct gc555_cta_view sink = {};
	u8 retained_vics[GC555_CTA_MAX_VICS] = {};
	bool retained_y420[GC555_CTA_MAX_VICS] = {};
	u8 block[GC555_CTA_SCRATCH_SIZE] = {};
	u8 source_sad[3] = {};
	u8 sink_sad[3] = {};
	unsigned int retained_count = 0;
	unsigned int offset = 4;
	unsigned int i;
	int ret;

	if (!output || !gc555_cta_parse(source_cta, &source) ||
	    !gc555_cta_parse(sink_cta, &sink))
		return -EBADMSG;

	memset(output, 0, GC555_EDID_BLOCK_SIZE);
	output[0] = 0x02;
	output[1] = 0x03;
	output[3] = source.flags & sink.flags & (BIT(7) | BIT(5) | BIT(4));

	if (sink.video.data) {
		for (i = 1; i <= sink.video.length; i++) {
			if (!gc555_cta_vic_allowed(sink.video.data[i]))
				continue;
			if (retained_count >= ARRAY_SIZE(retained_vics))
				return -E2BIG;
			retained_vics[retained_count] = sink.video.data[i];
			retained_y420[retained_count] =
				gc555_cta_svd_supports_y420(&sink, i);
			retained_count++;
		}
		if (retained_count) {
			block[0] = 0x40 | retained_count;
			memcpy(block + 1, retained_vics, retained_count);
			ret = gc555_cta_append(output, &offset, block,
					       retained_count + 1);
			if (ret)
				return ret;
		}
	}

	if (gc555_cta_find_lpcm(&source.audio, source_sad) &&
	    gc555_cta_find_lpcm(&sink.audio, sink_sad)) {
		u8 merged_sad[4] = { 0x23 };

		merged_sad[1] = min(source_sad[0], sink_sad[0]) & 0x0f;
		/* Limit LPCM rates to 32, 44.1, and 48 kHz. */
		merged_sad[2] = (source_sad[1] & sink_sad[1]) & 0x07;
		merged_sad[3] = (source_sad[2] & sink_sad[2]) & 0x07;
		if (merged_sad[2] && merged_sad[3]) {
			ret = gc555_cta_append(output, &offset, merged_sad,
					       sizeof(merged_sad));
			if (ret)
				return ret;
			output[3] |= BIT(6);
		}
	}

	if (source.speaker.data && source.speaker.length >= 1 &&
	    sink.speaker.data && sink.speaker.length >= 1) {
		u8 speaker[4] = {
			0x83,
			source.speaker.data[1] & sink.speaker.data[1] & 0x4f,
		};

		ret = gc555_cta_append(output, &offset, speaker,
				       sizeof(speaker));
		if (ret)
			return ret;
	}

	if (sink.hdmi_vsdb.data) {
		unsigned int size = sink.hdmi_vsdb.length + 1;

		if (size > sizeof(block))
			return -E2BIG;
		memcpy(block, sink.hdmi_vsdb.data, size);
		gc555_cta_limit_hdmi_vsdb(block, size);
		ret = gc555_cta_append(output, &offset, block, size);
		if (ret)
			return ret;
	}

	if (source.hf_vsdb.data && source.hf_vsdb.length >= 7 &&
	    sink.hf_vsdb.data && sink.hf_vsdb.length >= 7) {
		unsigned int size = source.hf_vsdb.length + 1;

		if (size > sizeof(block))
			return -E2BIG;
		memcpy(block, source.hf_vsdb.data, size);
		block[5] = min(source.hf_vsdb.data[5], sink.hf_vsdb.data[5]);
		block[6] &= sink.hf_vsdb.data[6];
		block[7] &= sink.hf_vsdb.data[7];
		ret = gc555_cta_append(output, &offset, block, size);
		if (ret)
			return ret;
	}

	if (sink.y420_only.data) {
		unsigned int count = 0;

		block[1] = 0x0e;
		for (i = 2; i <= sink.y420_only.length; i++) {
			u8 vic = sink.y420_only.data[i];

			if ((vic & 0x7f) != 96 && (vic & 0x7f) != 97)
				continue;
			block[2 + count++] = vic;
		}
		if (count) {
			block[0] = 0xe0 | (count + 1);
			ret = gc555_cta_append(output, &offset, block,
					       count + 2);
			if (ret)
				return ret;
		}
	}

	if (sink.y420_map.data && retained_count) {
		unsigned int map_bytes = DIV_ROUND_UP(retained_count, 8);

		memset(block, 0, sizeof(block));
		block[1] = 0x0f;
		for (i = 0; i < retained_count; i++) {
			if (retained_y420[i])
				block[2 + i / 8] |= BIT(i % 8);
		}
		while (map_bytes && !block[1 + map_bytes])
			map_bytes--;
		if (map_bytes) {
			block[0] = 0xe0 | (map_bytes + 1);
			ret = gc555_cta_append(output, &offset, block,
					       map_bytes + 2);
			if (ret)
				return ret;
		}
	}

	if (sink.colorimetry.data) {
		ret = gc555_cta_append(output, &offset,
				       sink.colorimetry.data,
				       sink.colorimetry.length + 1);
		if (ret)
			return ret;
	}
	if (sink.video_capability.data) {
		ret = gc555_cta_append(output, &offset,
				       sink.video_capability.data,
				       sink.video_capability.length + 1);
		if (ret)
			return ret;
	}
	if (sink.hdr_static.data) {
		ret = gc555_cta_append(output, &offset,
				       sink.hdr_static.data,
				       sink.hdr_static.length + 1);
		if (ret)
			return ret;
	}

	output[2] = offset;
	/* Keep GC555 timings ahead of sink timings in the preference order. */
	for (i = 0; i < source.dtd_count; i++) {
		ret = gc555_cta_append(output, &offset,
				       source.dtds + i * 18, 18);
		if (ret)
			break;
	}
	for (i = 0; i < sink.dtd_count; i++) {
		ret = gc555_cta_append(output, &offset, sink.dtds + i * 18, 18);
		if (ret)
			break;
	}
	gc555_edid_set_checksum(output);

	return 0;
}

int gc555_edid_merge(const u8 *sink, size_t sink_size,
		     u8 *merged, size_t merged_size)
{
	const u8 *source;
	const u8 *sink_cta = NULL;
	size_t source_size;
	unsigned int block;
	int ret;

	if (!sink || !merged || merged_size < GC555_EDID_SIZE ||
	    !gc555_edid_valid(sink, sink_size))
		return -EINVAL;

	for (block = 1; block < sink_size / GC555_EDID_BLOCK_SIZE; block++) {
		const u8 *candidate = sink + block * GC555_EDID_BLOCK_SIZE;

		if (candidate[0] == 0x02 && candidate[1] == 0x03) {
			sink_cta = candidate;
			break;
		}
	}
	if (!sink_cta)
		return -ENODATA;

	ret = gc555_edid_get(&source, &source_size);
	if (ret)
		return ret;
	if (source_size != GC555_EDID_SIZE)
		return -EINVAL;

	memcpy(merged, source, GC555_EDID_BLOCK_SIZE);
	ret = gc555_cta_merge(source + GC555_EDID_BLOCK_SIZE, sink_cta,
			      merged + GC555_EDID_BLOCK_SIZE);
	if (ret)
		return ret;
	if (!gc555_edid_valid(merged, GC555_EDID_SIZE))
		return -EBADMSG;

	return 0;
}

int gc555_edid_get(const u8 **edid, size_t *size)
{
	if (!edid || !size)
		return -EINVAL;
	if (!gc555_edid_valid(gc555_edid, sizeof(gc555_edid)))
		return -EINVAL;

	*edid = gc555_edid;
	*size = sizeof(gc555_edid);
	return 0;
}
