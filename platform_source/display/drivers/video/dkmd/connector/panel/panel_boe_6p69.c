/* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Code in this file used to drive the oled panel.
 * Author:DSS
 * Create: 2025
 */
#include "dpu_conn_mgr.h"
#include "panel_mgr.h"
#include "panel_drv.h"

#ifndef array_size
#define array_size(array) (sizeof(array) / sizeof((array)[0]))
#endif

/*
 * Power ON/OFF Sequence(sleep mode to Normal mode) begin
 */
static char g_on_cmd1[] = {
	0xFE, 0xD0,
};

static char g_on_cmd2[] = {
	0xB2, 0x64,
};

static char g_lvd_on[31][2] = {
	{0xFE, 0xA0},
	{0x06, 0x36},
	{0x7C, 0x15},
	{0xFE, 0xB0},
	{0x58, 0x00},
	{0xFE, 0x42},
	{0x78, 0x15},
	{0xFE, 0xD4},
	{0x40, 0x02},
	{0xFE, 0xFD},
	{0x80, 0x07},
	{0x83, 0x00},
	{0xFE, 0xA1},
	{0x74, 0x72},
	{0x7E, 0x20},
	{0xC3, 0x83},
	{0xC4, 0xFF},
	{0xC5, 0x7F},
	{0xFE, 0xD4},
	{0x42, 0x00},
	{0xFE, 0xFF},
	{0x63, 0x1C},
	{0x64, 0x1C},
	{0x65, 0x1C},
	{0x66, 0x1C},
	{0x6B, 0x00},
	{0x6C, 0x00},
	{0x6D, 0x00},
	{0x6E, 0x00},
	{0xFE, 0xD2},
	{0x97, 0x00},
};

static char g_fps_120[6][2] = {
	{0xFE, 0x00},
	{0x2F, 0x0C},
	{0x8A, 0x13},
	{0xFA, 0x07},
	{0xC2, 0x08},
	{0x35, 0x00},
};

static char g_fps_120_3[] = {
	0x51, 0x09, 0x60,
};

static char g_vgl_code[21][2] = {
	{0xFE, 0xD0},
	{0x12, 0x38},
	{0x23, 0xFF},
	{0xFE, 0x9B},
	{0x23, 0x7D},
	{0x25, 0x0F},
	{0x26, 0xA0},
	{0x28, 0x03},
	{0x62, 0x91},
	{0x53, 0x02},
	{0x54, 0x02},
	{0x66, 0x0C},
	{0x12, 0x00},
	{0x13, 0x00},
	{0x5E, 0x0C},
	{0x0E, 0x34},
	{0x5D, 0x0A},
	{0x0D, 0x07},
	{0xFE, 0x00},
	{0xFE, 0xFD},
	{0x90, 0x01},
};

static char g_temper_init[5][2] = {
	{0xFE, 0x42},
	{0x6B, 0xC9},
	{0x6C, 0x68},
	{0x6D, 0x01},
	{0xFE, 0x00},
};

static char g_rcn_off[3][2] = {
	{0xFE, 0x16},
	{0x6E, 0x00},
	{0xFE, 0x00},
};

static char g_on_cmd_end1[] = {
	0x11,
};

static char g_on_cmd_end2[] = {
	0x29,
};

#ifdef BIST_MODE
static char g_bist_on[18][2] = {
	{0xFE, 0x00},
	{0xFA, 0x07},
	{0xFE, 0x97},
	{0x33, 0x01},
	{0x34, 0x00},
	{0x35, 0x00},
	{0x36, 0x1F},
	{0xFE, 0xD0},
	{0x42, 0x04},
	{0xFE, 0x40},
	{0x15, 0x03},
	{0x16, 0x20},
	{0xFE, 0x97},
	{0x73, 0x0C},
	{0x3D, 0x05},
	{0x3F, 0x28},
	{0xFE, 0xD4},
	{0x0C, 0x00},
};
#endif

static struct dsi_cmd_desc g_display_on_cmds[] = {
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_on_cmd1},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd2), g_on_cmd2},

	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[3]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[4]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[5]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[6]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[7]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[8]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[9]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[10]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[11]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[12]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[13]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[14]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[15]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[16]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[17]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[18]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[19]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[20]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[21]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[22]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[23]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[24]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[25]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[26]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[27]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[28]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[29]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_lvd_on[30]},

	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[3]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[4]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_fps_120[5]},

	{DTYPE_DCS_LWRITE, 0, 10, WAIT_TYPE_US, sizeof(g_fps_120_3), g_fps_120_3},

	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[3]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[4]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[5]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[6]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[7]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[8]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[9]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[10]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[11]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[12]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[13]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[14]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[15]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[16]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[17]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[18]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[19]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_vgl_code[20]},

	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_temper_init[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_temper_init[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_temper_init[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_temper_init[3]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_temper_init[4]},

	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_rcn_off[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_rcn_off[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_rcn_off[2]},

	{DTYPE_DCS_WRITE, 0, 90, WAIT_TYPE_MS, sizeof(g_on_cmd_end1), g_on_cmd_end1},
	{DTYPE_DCS_WRITE, 0, 25, WAIT_TYPE_US, sizeof(g_on_cmd_end2), g_on_cmd_end2},

#ifdef BIST_MODE
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[3]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[4]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[5]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[6]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[7]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[8]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[9]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[10]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[11]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[12]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[13]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[14]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[15]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[16]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_on[17]},
#endif
};

/* Power OFF Sequence */
static char g_off_cmd0[] = {
	0xFE, 0x00,
};

static char g_off_cmd1[] = {
	0x28,
};

static char g_off_cmd2[] = {
	0x10,
};

#ifdef BIST_MODE
static char g_bist_off[4][2] = {
	{0xFE, 0x97},
	{0x33, 0x00},
	{0xFE, 0x00},
	{0xFA, 0x01},
};
#endif

static struct dsi_cmd_desc g_ldisplay_off_cmds[] = {
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_off_cmd0), g_off_cmd0 },
	{DTYPE_DCS_WRITE, 0, 34, WAIT_TYPE_US, sizeof(g_off_cmd1), g_off_cmd1 },
	{DTYPE_DCS_WRITE, 0, 100, WAIT_TYPE_MS, sizeof(g_off_cmd2), g_off_cmd2 },
#ifdef BIST_MODE
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_off[0]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_off[1]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_off[2]},
	{DTYPE_DCS_WRITE1, 0, 10, WAIT_TYPE_US, sizeof(g_on_cmd1), g_bist_off[3]},
#endif
};

/*******************************************************************************
 ** LCD GPIO
 */
#define GPIO_AMOLED_RESET_NAME   "gpio_amoled_reset"
#define GPIO_AMOLED_VCC1V2_NAME  "gpio_amoled_vcc1v2"
#define GPIO_AMOLED_VCC1V8_NAME  "gpio_amoled_vcc1v8"
#define GPIO_AMOLED_VCC3V1_NAME  "gpio_amoled_vcc3v1"
#define GPIO_AMOLED_PMICRST_NAME "gpio_amoled_pmicrst"
#define GPIO_AMOLED_TE0_NAME     "gpio_amoled_te0"

static uint32_t g_gpio_amoled_reset;
static uint32_t g_gpio_amoled_vcc1v2;
static uint32_t g_gpio_amoled_vcc1v8;
static uint32_t g_gpio_amoled_vcc3v1;
static uint32_t g_gpio_amoled_pmicrst;
static uint32_t g_gpio_amoled_te0;

static struct gpio_desc g_asic_lcd_gpio_request_cmds[] = {
	{DTYPE_GPIO_REQUEST, WAIT_TYPE_MS, 0, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 0},
	{DTYPE_GPIO_REQUEST, WAIT_TYPE_MS, 0, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 0},
	{DTYPE_GPIO_REQUEST, WAIT_TYPE_MS, 0, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 0},
	{DTYPE_GPIO_REQUEST, WAIT_TYPE_MS, 0, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
};

static struct gpio_desc g_fpga_lcd_gpio_normal_cmds[] = {
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_VCC1V8_NAME, &g_gpio_amoled_vcc1v8, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 35, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 1},
};

static struct gpio_desc g_asic_lcd_gpio_normal_cmds[] = {
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 2, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 6, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 1},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 35, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 1},
};

static struct gpio_desc g_fpga_lcd_gpio_lowpower_cmds[] = {
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_VCC1V8_NAME, &g_gpio_amoled_vcc1v8, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 20, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 0},
};

static struct gpio_desc g_asic_lcd_gpio_lowpower_cmds[] = {
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 10, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 10, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 0},
	{DTYPE_GPIO_OUTPUT, WAIT_TYPE_MS, 10, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 0},
};

static struct gpio_desc g_asic_lcd_gpio_free_cmds[] = {
	{DTYPE_GPIO_FREE, WAIT_TYPE_MS, 5, GPIO_AMOLED_RESET_NAME, &g_gpio_amoled_reset, 0},
	{DTYPE_GPIO_FREE, WAIT_TYPE_MS, 10, GPIO_AMOLED_VCC3V1_NAME, &g_gpio_amoled_vcc3v1, 0},
	{DTYPE_GPIO_FREE, WAIT_TYPE_MS, 10, GPIO_AMOLED_VCC1V2_NAME, &g_gpio_amoled_vcc1v2, 0},
	{DTYPE_GPIO_FREE, WAIT_TYPE_MS, 10, GPIO_AMOLED_PMICRST_NAME, &g_gpio_amoled_pmicrst, 0},
};

/*******************************************************************************
 * LCD VCC
 */
#define VCC_LCDIO_NAME	"lcdio-vcc"
static struct regulator *g_vcc_lcdio;
 
static struct vcc_desc g_lcd_vcc_init_cmds[] = {
	/* vcc get */
	{ DTYPE_VCC_GET, VCC_LCDIO_NAME,
		&g_vcc_lcdio, 0, 0, WAIT_TYPE_MS, 0 },
 
	/* io set voltage */
	{ DTYPE_VCC_SET_VOLTAGE, VCC_LCDIO_NAME,
		&g_vcc_lcdio, 1850000, 1850000, WAIT_TYPE_MS, 0 },
};
 
static struct vcc_desc g_lcd_vcc_finit_cmds[] = {
	/* vcc put */
	{ DTYPE_VCC_PUT, VCC_LCDIO_NAME,
		&g_vcc_lcdio, 0, 0, WAIT_TYPE_MS, 0 },
};
 
static struct vcc_desc g_lcd_vcc_enable_cmds[] = {
	/* vcc enable */
	{ DTYPE_VCC_ENABLE, VCC_LCDIO_NAME,
		&g_vcc_lcdio, 0, 0, WAIT_TYPE_MS, 3 },
};
 
static struct vcc_desc g_lcd_vcc_disable_cmds[] = {
	/* vcc disable */
	{ DTYPE_VCC_DISABLE, VCC_LCDIO_NAME,
		&g_vcc_lcdio, 0, 0, WAIT_TYPE_MS, 3 },
};


/*******************************************************************************
 */
static void panel_drv_private_data_setup(struct panel_drv_private *priv, struct device_node *np)
{
	if (priv->connector_info.base.fpga_flag == 1) {
		g_gpio_amoled_vcc3v1 = (uint32_t)of_get_named_gpio(np, "gpios", 0);
		g_gpio_amoled_vcc1v8 = (uint32_t)of_get_named_gpio(np, "gpios", 1);
		g_gpio_amoled_vcc1v2 = (uint32_t)of_get_named_gpio(np, "gpios", 2);
		g_gpio_amoled_reset = (uint32_t)of_get_named_gpio(np, "gpios", 3);
		g_gpio_amoled_te0 = (uint32_t)of_get_named_gpio(np, "gpios", 4);
		g_gpio_amoled_pmicrst = (uint32_t)of_get_named_gpio(np, "gpios", 5);

		priv->gpio_normal_cmds = g_fpga_lcd_gpio_normal_cmds;
		priv->gpio_normal_cmds_len = array_size(g_fpga_lcd_gpio_normal_cmds);
		priv->gpio_lowpower_cmds = g_fpga_lcd_gpio_lowpower_cmds;
		priv->gpio_lowpower_cmds_len = array_size(g_fpga_lcd_gpio_lowpower_cmds);
	} else {
		g_gpio_amoled_vcc3v1 = (uint32_t)of_get_named_gpio(np, "gpios", 0);
		g_gpio_amoled_vcc1v2 = (uint32_t)of_get_named_gpio(np, "gpios", 1);
		g_gpio_amoled_reset = (uint32_t)of_get_named_gpio(np, "gpios", 2);
		g_gpio_amoled_te0 = (uint32_t)of_get_named_gpio(np, "gpios", 3);
		g_gpio_amoled_pmicrst = (uint32_t)of_get_named_gpio(np, "gpios", 4);

		priv->vcc_init_cmds = g_lcd_vcc_init_cmds;
		priv->vcc_init_cmds_len = array_size(g_lcd_vcc_init_cmds);
		priv->vcc_finit_cmds = g_lcd_vcc_finit_cmds;
		priv->vcc_finit_cmds_len = array_size(g_lcd_vcc_finit_cmds);

		priv->vcc_enable_cmds = g_lcd_vcc_enable_cmds;
		priv->vcc_enable_cmds_len = array_size(g_lcd_vcc_enable_cmds);
		priv->vcc_disable_cmds = g_lcd_vcc_disable_cmds;
		priv->vcc_disable_cmds_len = array_size(g_lcd_vcc_disable_cmds);

		priv->gpio_request_cmds = g_asic_lcd_gpio_request_cmds;
		priv->gpio_request_cmds_len = array_size(g_asic_lcd_gpio_request_cmds);
		priv->gpio_free_cmds = g_asic_lcd_gpio_free_cmds;
		priv->gpio_free_cmds_len = array_size(g_asic_lcd_gpio_free_cmds);

		priv->gpio_normal_cmds = g_asic_lcd_gpio_normal_cmds;
		priv->gpio_normal_cmds_len = array_size(g_asic_lcd_gpio_normal_cmds);
		priv->gpio_lowpower_cmds = g_asic_lcd_gpio_lowpower_cmds;
		priv->gpio_lowpower_cmds_len = array_size(g_asic_lcd_gpio_lowpower_cmds);
	}
}

/* dsi param initialized value from panel spec */
static void mipi_lcd_init_dsi_param(struct dkmd_connector_info *pinfo, struct mipi_panel_info *mipi)
{
	if (pinfo->base.fpga_flag == 1) {
		mipi->hsa = 4;
		mipi->hbp = 16;
		mipi->dpi_hsize = 914;
		mipi->hline_time = 942;

		mipi->vsa = 8;
		mipi->vbp = 92;
		mipi->vfp = 80;
		mipi->pxl_clk_rate = 20 * 1000000UL;
		mipi->dsi_bit_clk = 120;
	} else {
		mipi->hsa = 4;
		mipi->hbp = 16;
		mipi->dpi_hsize = 914;
		mipi->hline_time = 942;

		mipi->vsa = 8;
		mipi->vbp = 92;
		mipi->vfp = 80;
		mipi->pxl_clk_rate = 192 * 1000000UL;
		mipi->dsi_bit_clk = 499;
	}

	mipi->dsi_bit_clk_upt = mipi->dsi_bit_clk;
	mipi->dsi_bit_clk_default = mipi->dsi_bit_clk;

	mipi->pxl_clk_rate_div = 1;
	mipi->dsi_bit_clk_upt_support = 0;

	mipi->clk_post_adjust = 16;
	mipi->lane_nums = DSI_4_LANES;
	mipi->color_mode = DSI_24BITS_1;
	mipi->phy_mode = DPHY_MODE; // DPHY_MODE;

	/* for video mode */
	mipi->vc = 0;
	mipi->max_tx_esc_clk = 10 * 1000000;
	mipi->burst_mode = DSI_BURST_SYNC_PULSES_1;
	mipi->non_continue_en = 1;
}

static int32_t panel_of_device_setup(struct panel_drv_private *priv)
{
	int32_t ret;
	struct dkmd_connector_info *pinfo = &priv->connector_info;
	struct device_node *np = priv->pdev->dev.of_node;
	
	dpu_pr_info("enter!\n");

	/* Inheritance based processing */
	panel_base_of_device_setup(priv);
	panel_drv_private_data_setup(priv, np);

	pinfo->base.type = PANEL_MIPI_CMD;

	/* 1. config base object info
	 * would be used for framebuffer setup
	 */
	pinfo->base.xres = 1216;
	pinfo->base.yres = 2688;

	/* When calculating DPI needs the following parameters */
	pinfo->base.width = 70;
	pinfo->base.height = 154;

	/* ltpo parameter init */
	priv->dfr_info.dfr_mode = DFR_MODE_CONSTANT;
	priv->dfr_info.ddic_type = DDIC_TYPE_INVALID;
	priv->dfr_info.oled_info.oled_type = PANEL_OLED_LTPS;
	// caculate fps by mipi timing para
	pinfo->base.fps = 60;

	/* 2. config connector info
	 * would be used for dsi & composer setup
	 */
	mipi_lcd_init_dsi_param(pinfo, &get_primary_connector(pinfo)->post_info[0]->mipi);

	/* dsi or composer need this param */
	pinfo->dirty_region_updt_support = 0;
	pinfo->vsync_ctrl_type = 0;
	/* 3. config panel private info
	 * would be used for panel setup
	 */
	pinfo->bl_info.bl_min = 3;
	pinfo->bl_info.bl_max = 4095;
	pinfo->bl_info.bl_default = 1605;
	priv->mipi_brightness_para_type = MIPI_BL_PARA1_DBV8_AND_PARA2_DBV0;

	/* mipi dsi cmds */
	priv->disp_on_cmds = g_display_on_cmds;
	priv->disp_on_cmds_len = (uint32_t)ARRAY_SIZE(g_display_on_cmds);
	priv->disp_off_cmds = g_ldisplay_off_cmds;
	priv->disp_off_cmds_len = (uint32_t)ARRAY_SIZE(g_ldisplay_off_cmds);

	if (pinfo->base.fpga_flag == 0) {
		ret = peri_vcc_cmds_tx(priv->pdev, priv->vcc_init_cmds, priv->vcc_init_cmds_len);
		if (ret != 0)
			dpu_pr_info("vcc init failed!\n");
 
		ret = peri_pinctrl_cmds_tx(priv->pdev, priv->pinctrl_init_cmds, priv->pinctrl_init_cmds_len);
		if (ret != 0)
			dpu_pr_info("pinctrl init failed\n");
 
		ret = peri_vcc_cmds_tx(priv->pdev, priv->vcc_enable_cmds, priv->vcc_enable_cmds_len);
		if (ret)
			dpu_pr_warn("vcc enable cmds handle fail!\n");
	}

	dpu_pr_info("exit!\n");

	return 0;
}

static void panel_of_device_release(struct panel_drv_private *priv)
{
	int32_t ret = 0;
	panel_base_of_device_release(priv);
	if (priv->gpio_free_cmds && (priv->gpio_free_cmds_len > 0)) {
		ret = peri_gpio_cmds_tx(priv->gpio_free_cmds, priv->gpio_free_cmds_len);
		if (ret)
			dpu_pr_info("gpio free handle err!\n");
	}
	dpu_pr_info("exit!\n");
}

panel_device_match_data(boe_6p69_panel_info, PANEL_BOE_6P69_ID, panel_of_device_setup, panel_of_device_release);

MODULE_LICENSE("GPL");