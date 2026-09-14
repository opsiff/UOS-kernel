/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __EDID_PRIVATE_H__
#define __EDID_PRIVATE_H__

#include "dkmd_edid_interface.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmacro-redefined"

#define upper_nibble(x)  ((0xF0 & (x)) >> 4)
#define lower_nibble(x)  (0xF & (x))
#define combine_hi_8lo(hi, lo)  ((((uint8_t)(hi)) << 8) | (uint8_t)(lo))
#define combine_hi_4lo(hi, lo)  ((((uint8_t)(hi)) << 4) | (uint8_t)(lo))
#define dp_imonitor_set_param(a, b)
#define get_cea_data_block_len(x) ((uint8_t)0x1F & (x)[0])
#define get_cea_data_block_tag(x) (((uint8_t)0xE0 & (x)[0]) >> 5)

#define EDID_LENGTH                 0x80
#define EDID_MAX_BLOCK_NUM          8
#define DEFAULT_EDID_BUFLENGTH		(128UL)

#define EDID_HEADER	                0x00
#define EDID_HEADER_END	            0x07

#define EXTENSION_HEADER_CEA            0x02
#define EXTENSION_HEADER_DISPLAYID     0x70

#define EDID_FACTORY_START          0x08
#define EDID_FACTORY_END		    0x11

#define EDID_STRUCT_VERSION         0x12
#define EDID_STRUCT_REVISION        0x13

#define EDID_INPUT_DEFINITION		0x14

#define HORIZONTAL_SCREEN_SIZE	0x15
#define VERTICAL_SCREEN_SIZE	0x16

#define ESTABLISHED_TIMINGS_START	0x23
#define ESTABLISHED_TIMINGS_BIT_SIZE 17
#define ESTABLISHED_TIMINGS(x)		BIT(x)

#define DETAILED_TIMING_DESCRIPTIONS_START  0x36
#define DETAILED_TIMING_DESCRIPTION_SIZE    18
#define DETAILED_TIMING_DESCRIPTION_COUNT	4
#define MONITOR_NAME                0xfc
#define MONITOR_LIMITS              0xfd
#define MONITOR_SERIAL_NUMBER		0Xff
#define EXTENSION_FLAG              0x7e
#define UNKNOWN_DESCRIPTOR          (-1)
#define DETAILED_TIMING_BLOCK       (-2)

#define CTA_EEODB_FLAG_INDEX1       0x04
#define CTA_EEODB_FLAG_INDEX2       0x05
#define CTA_EEODB_LENGTH_INDEX      0x06
#define CTA_EEODB_FLAG_ID1          0xE2
#define CTA_EEODB_FLAG_ID2          0x78

#define MONITOR_NUMBER_DESCRIPTION_SIZE	 14

#define H_ACTIVE_LO(x)         (uint8_t)((x)[2])
#define H_ACTIVE_HI(x)         upper_nibble((uint8_t)(x)[4])
#define H_ACTIVE(x)            combine_hi_8lo(H_ACTIVE_HI(x), H_ACTIVE_LO(x))
#define V_ACTIVE_LO(x)         (uint8_t)((x)[5])
#define V_ACTIVE_HI(x)         upper_nibble((uint8_t)(x)[7])
#define V_ACTIVE(x)            combine_hi_8lo(V_ACTIVE_HI(x), V_ACTIVE_LO(x))

#define V_MIN_RATE(x)                  (x)[5]
#define V_MAX_RATE(x)                  (x)[6]
#define H_MIN_RATE(x)                  (x)[7]
#define H_MAX_RATE(x)                  (x)[8]
#define MAX_PIXEL_CLOCK(x)             (((uint64_t)(x)[9]) * 10)

#define PIXEL_CLOCK_LO(x)      (uint8_t)((x)[0])
#define PIXEL_CLOCK_HI(x)      (uint8_t)((x)[1])
#define PIXEL_CLOCK(x)         (combine_hi_8lo(PIXEL_CLOCK_HI(x), PIXEL_CLOCK_LO(x)))

#define H_SYNC_OFFSET_LO(x)    	(uint8_t)((x)[8])
#define H_SYNC_OFFSET_HI(x)    	(((uint8_t)(x)[11] & (0x40 | 0x80)) >> 6)
#define H_SYNC_OFFSET(x)       	combine_hi_8lo(H_SYNC_OFFSET_HI(x), H_SYNC_OFFSET_LO(x))
#define V_SYNC_OFFSET_LO(x)		upper_nibble((uint8_t)(x)[10])
#define V_SYNC_OFFSET_HI(x)		(((uint8_t)(x)[11] & (0x4 | 0x8)) >> 2)
#define V_SYNC_OFFSET(x)		combine_hi_4lo(V_SYNC_OFFSET_HI(x), V_SYNC_OFFSET_LO(x))

#define H_SYNC_WIDTH_LO(x)		(uint8_t)((x)[9])
#define H_SYNC_WIDTH_HI(x)		(((uint8_t)(x)[11] & (0x10 | 0x20)) >> 4)
#define H_SYNC_WIDTH(x)       	combine_hi_8lo(H_SYNC_WIDTH_HI(x), H_SYNC_WIDTH_LO(x))
#define V_SYNC_WIDTH_HI(x)		((uint8_t)(x)[11] & (0x1 | 0x2))
#define V_SYNC_WIDTH_LO(x)		lower_nibble((uint8_t)(x)[10])
#define V_SYNC_WIDTH(x)			combine_hi_4lo(V_SYNC_WIDTH_HI(x), V_SYNC_WIDTH_LO(x))

#define H_SIZE_LO(x)			(uint8_t)((x)[12])
#define V_SIZE_LO(x)			(uint8_t)((x)[13])
#define H_SIZE_HI(x)			upper_nibble((uint8_t)(x)[14])
#define V_SIZE_HI(x)			lower_nibble((uint8_t)(x)[14])
#define H_SIZE(x)				combine_hi_8lo(H_SIZE_HI(x), H_SIZE_LO(x))
#define V_SIZE(x)				combine_hi_8lo(V_SIZE_HI(x), V_SIZE_LO(x))

#define H_BORDER(x)			(uint8_t)((x)[15])
#define V_BORDER(x)			(uint8_t)((x)[16])

#define H_BLANKING_LO(x)       (uint8_t)((x)[3])
#define H_BLANKING_HI(x)       lower_nibble((uint8_t)(x)[4])
#define H_BLANKING(x)          combine_hi_8lo(H_BLANKING_HI(x), H_BLANKING_LO(x))
#define V_BLANKING_LO(x)       (uint8_t)((x)[6])
#define V_BLANKING_HI(x)       lower_nibble((uint8_t)(x)[7])
#define V_BLANKING(x)          combine_hi_8lo(V_BLANKING_HI(x), V_BLANKING_LO(x))

#define FLAGS(x)				(uint8_t)((x)[17])
#define INPUT_TYPE(x)			(((FLAGS(x) & 0x60) >> 4) | ((FLAGS(x) & 0x1)))
#define VSCAN_MODE(x)			((FLAGS(x) & 0x80) >> 7)
#define V_SYNC_POLARITY(x)     	((FLAGS(x) & 0x4) >> 2)
#define H_SYNC_POLARITY(x)     	((FLAGS(x) & 0x2) >> 1)
#define SYNC_SCHEME(x)			((FLAGS(x) & 0x18) >> 3)
#define SCHEME_DETAIL(x)		((FLAGS(x) & 0x6) >> 1)

#define H_ACTIVE_LO_DISPLAYID(x)		(uint8_t)((x)[7])
#define H_ACTIVE_HI_DISPLAYID(x)		((uint8_t)(x)[8])
#define H_ACTIVE_DISPLAYID(x)			(combine_hi_8lo(H_ACTIVE_HI_DISPLAYID(x), H_ACTIVE_LO_DISPLAYID(x)) + 1)
#define V_ACTIVE_LO_DISPLAYID(x)     	(uint8_t)((x)[15])
#define V_ACTIVE_HI_DISPLAYID(x)     	((uint8_t)(x)[16])
#define V_ACTIVE_DISPLAYID(x)        	(combine_hi_8lo(V_ACTIVE_HI_DISPLAYID(x), V_ACTIVE_LO_DISPLAYID(x)) + 1)

#define PIXEL_CLOCK_DISPLAYID(x)     ((((((uint32_t)(x)[5]) << 16) | ((uint32_t)(x)[4]) << 8 | (uint32_t)(x)[3]) + 1))

#define H_BLANKING_LO_DISPLAYID(x)   	(uint8_t)((x)[9])
#define H_BLANKING_HI_DISPLAYID(x)	((uint8_t)(x)[10])
#define H_BLANKING_DISPLAYID(x)      	(combine_hi_8lo(H_BLANKING_HI_DISPLAYID(x), H_BLANKING_LO_DISPLAYID(x)) + 1)
#define V_BLANKING_LO_DISPLAYID(x)   	(uint8_t)((x)[17])
#define V_BLANKING_HI_DISPLAYID(x)   	((uint8_t)(x)[18])
#define V_BLANKING_DISPLAYID(x)      	(combine_hi_8lo(V_BLANKING_HI_DISPLAYID(x), V_BLANKING_LO_DISPLAYID(x)) + 1)

#define H_SYNC_OFFSET_LO_DISPLAYID(x)    	(uint8_t)((x)[11])
#define H_SYNC_OFFSET_HI_DISPLAYID(x)    	(uint8_t)((x)[12])
#define H_SYNC_OFFSET_DISPLAYID(x)      	(combine_hi_8lo(H_SYNC_OFFSET_HI_DISPLAYID(x), H_SYNC_OFFSET_LO_DISPLAYID(x)) + 1)
#define V_SYNC_OFFSET_LO_DISPLAYID(x)		(uint8_t)((x)[19])
#define V_SYNC_OFFSET_HI_DISPLAYID(x)		(uint8_t)((x)[20])
#define V_SYNC_OFFSET_DISPLAYID(x)       	(combine_hi_8lo(V_SYNC_OFFSET_HI_DISPLAYID(x), V_SYNC_OFFSET_LO_DISPLAYID(x)) + 1)

#define H_SYNC_WIDTH_LO_DISPLAYID(x)     	(uint8_t)((x)[13])
#define H_SYNC_WIDTH_HI_DISPLAYID(x)     	(uint8_t)((x)[14])
#define H_SYNC_WIDTH_DISPLAYID(x)        	(combine_hi_8lo(H_SYNC_WIDTH_HI_DISPLAYID(x), H_SYNC_WIDTH_LO_DISPLAYID(x)) + 1)
#define V_SYNC_WIDTH_LO_DISPLAYID(x)	    (uint8_t)((x)[21])
#define V_SYNC_WIDTH_HI_DISPLAYID(x)	    (uint8_t)((x)[22])
#define V_SYNC_WIDTH_DISPLAYID(x)			(combine_hi_8lo(V_SYNC_WIDTH_HI_DISPLAYID(x), V_SYNC_WIDTH_LO_DISPLAYID(x)) + 1)

#define VIDEO_TIMING_SUPPORT_FLAG(x)		(uint8_t)((x)[10])
#define GTF2_START_FREQ(x)					(uint16_t)((x)[12] * 2)
#define GTF2_C(x)							(uint16_t)((x)[13])
#define GTF2_M(x)							(uint16_t)((x)[14] | ((x)[15] << 8))
#define GTF2_K(x)							(uint16_t)((x)[16])
#define GTF2_J(x)							(uint16_t)((x)[17])

#define EXTENSION_AUDIO_TAG         1
#define EXTENSION_VIDEO_TAG         2
#define EXTENSION_VENDOR_TAG        3
#define EXTENSION_SPEAKER_TAG       4

#define EXTEN_AUDIO_FORMAT(x)          ((0x78 & (x)[0]) >> 3)
#define EXTEN_AUDIO_MAX_CHANNELS(x)    ((0x7 & (x)[0]) + 1)
#define EXTEN_AUDIO_SAMPLING(x)        (0x7F & (x)[1])
#define EXTEN_AUDIO_LPCM_BIT(x)        (0x7 & (x)[2])
#define EXTEN_AUDIO_BITRATE(x)	       ((uint8_t)(x)[2] * 8)

#define EXTEN_SPEAKER(x)			   (0x7F & (x)[0])

#define EXTEN_VIDEO_CODE(x)		   ((uint8_t)0x7F & *(x))
#define EXTEN_VIDEO_NATIVE(x)		   (((uint8_t)0x80 & *(x)) >> 7)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef dtd_array_size
#define dtd_array_size(_A) (sizeof(_A) / sizeof((_A)[0]))
#endif /* dtd_array_size */

#define EDID_FEATURE_SUPPORT		0x18
#define EDID_FEATURE_SUPPORT_TIMING_MODE	BIT(0)
#define DEFAULT_GTF_SUPPORTED		0x00
#define RANGE_LIMITS_ONLY			0x01
#define SECONDARY_GTF_SUPPORTED		0x02
#define CVT_SUPPORTED				0x04
#define STANDARD_TIMINGS_START		0x26
#define IMAGE_ASPECT_RATIO_INDEX(x)		((0xC0 & (x)) >> 6)
#define REFRESH_RATE(x)				(0x3F & (x))
#define EDID_BASE_FPS				60

/* CVT Reduced Blanking Timing Parameter */
/* In pixel */
#define CVT_RBV2_MIN_H_BLANK		80
#define CVT_RBV2_H_SYNC				32
#define CVT_RBV2_H_FRONT_PORCH		8
/* In us */
#define CVT_RBV2_MIN_V_BLANK		460
/* In line */
#define CVT_RBV2_V_SYNC				8
#define CVT_RBV2_MIN_V_FRONT_PORCH	1
#define CVT_RBV2_MIN_V_BACK_PORCH	6
/* 1 indicates positive, 0 indicates negative */
#define CVT_RBV2_H_POLARITY			1
#define CVT_RBV2_V_POLARITY			0

/* GTF Blanking Timing Parameter */
/* The size of the top and bottom overscan margin as a percentage(1.8%) of the active vertical image */
#define	GTF_MARGIN_PERCENT			18
/* The assumed character cell granularity of the graphics system (in pixel) */
#define GTF_CELL_GRAN				8
/* The minimum vertical front porch in lines */
#define GTF_MIN_V_FRONT_PORCH		1
/* The width of the V sync in lines */
#define GTF_V_SYNC					3
/* The width of the H sync as a percentage(8%) of the total line period */
#define GTF_H_SYNC_PERCENT			8
/* Minimum time of vertical sync + back porch interval (us) */
#define GTF_MIN_V_SYNC_PLUS_BP		550
/* The default blanking formula gradient(600%/kHz) */
#define GTF_DEFAULT_M            	600
/* The default blanking formula offset(40%) */
#define GTF_DEFAULT_C				40
/* The default blanking formula scaling factor */
#define GTF_DEFAULT_K				128
/* The blanking formula scaling factor weighting(20%) */
#define GTF_DEFAULT_J				20

enum timing_mode {
	VESA_DMT_MODE = 0,
	VESA_CVT_MODE,
	VESA_GTF_MODE,
    VESA_GTF2_MODE
};

enum cvt_version {
	CVT_CRT = 0,
	CVT_RBV1,
	CVT_RBV2,
	CVT_RBV3
};

enum gtf_version {
	GTF_DEFAULT = 0,
	GTF_SECONDARY,
};

struct image_ration_t {
	uint8_t h_ratio;
	uint8_t v_ratio;
};

struct hdmi_vic {
    struct list_head list_node;
    uint32_t vic_id;
};

struct vfp_data {
    uint32_t refresh_rate;
    uint16_t v_blanking;
    uint16_t v_sync_offset;
};

struct vary_vfp_data {
    uint8_t code;
    uint8_t valid_count;
    struct vfp_data data[2];
};

struct dtd_info {
    uint8_t size;
    const struct dtd *support_modes_dtd;
};

struct cea_block_info {
    uint32_t reversion;
    const uint8_t *dtd_block;
    const uint8_t *cea_block;
    uint8_t dtd_start_byte;
    uint8_t cea_data_block_collection;
    uint16_t dtd_total;
};

static const uint8_t edid_v1_header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

#endif
#pragma GCC diagnostic pop