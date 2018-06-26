/*
 * MAXIM  max9286-max96705 GMSL driver
 *
 * Copyright (C) 2015-2017 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/videodev2.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/if.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>
#include "max9286_max96705.h"

//static char BWS[]="BWS";
//static char HIM[]="HIM";

static char GP1[]="GP1";
static char GP0[]="GP0";
//static char LOCK[]="LOCK";   //443
static char PWDN[]="PWDN";
static char FSYNC[]="FSYNC";

static int irq_max9286=1;
static int lock_max9286 = 0;
static u8 link0, link1, link2, link3;
static u8 tmp_link0, tmp_link1, tmp_link2, tmp_link3;

#define MAX9286_I2C_ID            0x48
#define MAX96705_I2C_ID           0x40
#define ISP_I2C_ADDR                0x5d
#define get_ds()		(KERNEL_DS)
#define get_fs()		(current_thread_info()->addr_limit)

//my debug code
#define __MYDEBUG__ 
#ifdef __MYDEBUG__ 
#define MYDEBUG(format,...) printk("File: "__FILE__", Line: %05d: "format"\n", __LINE__, ##__VA_ARGS__) 
#else 
#define MYDEBUG(format,...) 
#endif 

#define LINK_NUM           4
#define CAMERA_WIDTH		1280
#define CAMERA_HEIGHT 	720
#define I2C_SMBUS_MY	0x9999	/* SMBus transfer */

#define MAXIM_I2C_I2C_SPEED_837KHZ	(0x7 << 2) /* 837kbps */
#define MAXIM_I2C_I2C_SPEED_533KHZ	(0x6 << 2) /* 533kbps */
#define MAXIM_I2C_I2C_SPEED_339KHZ	(0x5 << 2) /* 339 kbps */
#define MAXIM_I2C_I2C_SPEED_173KHZ	(0x4 << 2) /* 174kbps */
#define MAXIM_I2C_I2C_SPEED_105KHZ	(0x3 << 2) /* 105 kbps */
#define MAXIM_I2C_I2C_SPEED_085KHZ	(0x2 << 2) /* 84.7 kbps */
#define MAXIM_I2C_I2C_SPEED_028KHZ	(0x1 << 2) /* 28.3 kbps */
#define MAXIM_I2C_I2C_SPEED		MAXIM_I2C_I2C_SPEED_339KHZ

struct max9286_max96705_priv {
	struct v4l2_subdev	sd[4];
	struct device_node	*sd_of_node[4];
	struct platform_device *pdev;
	int			des_addr;
	int			des_quirk_addr; /* second MAX9286 on the same I2C bus */
	int			links;
	int			links_mask;
	int			lanes;
	int			csi_rate;
	const char		*fsync_mode;
	int			fsync_period;
	char			pclk_rising_edge;
 	int			gpio_resetb;
	int			active_low_resetb;
	int			timeout;
	atomic_t		use_count;
	struct i2c_client	*client;
	int			max96705_addr_map[4];

	int					irq;
	int				width;
	int				height;
	v4l2_std_id				curr_norm;
	enum v4l2_field				field;
	struct device				*dev;
	struct media_pad			pad;
	/* mutual excl. when accessing chip */
	struct mutex				mutex;
	bool					streaming;
};

enum decoder_input_interface {
	DECODER_INPUT_INTERFACE_RGB888,
	DECODER_INPUT_INTERFACE_YCBCR422,
};

struct workqueue_struct *max96705_work ;  
//定义一个工作队列结构体  

struct max9286_link_config {
	enum decoder_input_interface	input_interface;
	
	int (*init_device)(void *);
	int (*init_controls)(void *);
	int (*s_power)(void *);
	int (*s_ctrl)(void *);
	int (*enum_mbus_code)(void *);
	int (*set_pad_format)(void *);
	int (*get_pad_format)(void *);
	int (*s_std)(void *);
	int (*querystd)(void *);
	int (*g_input_status)(void *);
	int (*s_routing)(void *);
	int (*g_mbus_config)(void *);

	struct device	*dev;
};

struct max9286_state {
	struct v4l2_subdev			sd;
	struct media_pad			pad;
	/* mutual excl. when accessing chip */
	struct mutex				mutex;
	int					irq;
	spinlock_t lock;
	v4l2_std_id				curr_norm;
	bool					streaming;
	const struct max9286_color_format	*cfmt;
	u32					width;
	u32					height;

	struct i2c_client			*client;
	unsigned int				register_page;
	struct i2c_client			*csi_client;
	enum v4l2_field				field;

	struct device				*dev;
	struct max9286_link_config		mipi_csi2_link[2];
	struct work_struct max_work;
};

static inline struct max9286_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct max9286_state, sd);
}

static int max9286_g_std(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct max9286_state *state = to_state(sd);

	*norm = state->curr_norm;

	return 0;
}

static int max9286_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	
	return 0;
}

static int max9286_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	
	return 0;
}

static int max9286_g_input_status(struct v4l2_subdev *sd, u32 *status)
{

	return 0;
}

static int max9286_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	*norm = V4L2_STD_ALL;
	return 0;
}

static int max9286_g_mbus_config(struct v4l2_subdev *sd,
								 struct v4l2_mbus_config *cfg)
{

	cfg->flags = V4L2_MBUS_CSI2_LANES |
		V4L2_MBUS_CSI2_CHANNELS |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	cfg->type = V4L2_MBUS_CSI2;

	return 0;
}

static int max9286_set_power(struct v4l2_subdev *sd, int enable)
{
	struct max9286_state *state = to_state(sd);
	if (enable) {
		reg8_write(state->client, 0x15, 0x9b);	
	} else {
		reg8_write(state->client, 0x15, 0x03);		
	}

	return 0;
}

static int max9286_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct max9286_state *state = to_state(sd);
	int ret;

	ret = mutex_lock_interruptible(&state->mutex);
	if (ret)
		return ret;
	ret = max9286_set_power(sd, enable);
	if (ret)
		goto out;

	state->streaming = enable;

out:
	mutex_unlock(&state->mutex);
	return ret;
}

static int max9286_enum_mbus_code(struct v4l2_subdev *sd,
								  struct v4l2_subdev_pad_config *cfg,
								  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	
	code->code = MEDIA_BUS_FMT_UYVY8_2X8;
		
	return 0;
}

static int max9286_set_pad_format(struct v4l2_subdev *sd,
								  struct v4l2_subdev_pad_config *cfg,
								  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	int ret = 0;

	mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *mf;
		return 0;
	}

	mf->width = CAMERA_WIDTH;
	mf->height = CAMERA_HEIGHT;


	return ret;
}

static int max9286_get_pad_format(struct v4l2_subdev *sd,
								  struct v4l2_subdev_pad_config *cfg,
								  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	mf->width = CAMERA_WIDTH;
	mf->height = CAMERA_HEIGHT;
	mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
	mf->colorspace = V4L2_COLORSPACE_SMPTE170M;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_video_ops max9286_video_ops = {
	.g_std		= max9286_g_std,
	.s_std		= max9286_s_std,
	.querystd	= max9286_querystd,
	.g_input_status = max9286_g_input_status,
	.g_tvnorms	= max9286_g_tvnorms,
	.g_mbus_config	= max9286_g_mbus_config,
	.s_stream	= max9286_s_stream,
};

static const struct v4l2_subdev_pad_ops max9286_pad_ops = {
	.enum_mbus_code = max9286_enum_mbus_code,
	.set_fmt = max9286_set_pad_format,
	.get_fmt = max9286_get_pad_format,
};

static const struct v4l2_subdev_ops max9286_ops = {
	.video = &max9286_video_ops,
	.pad = &max9286_pad_ops,
};

static int max9286_write_register(struct i2c_client *client,u8 addr, u8 reg, u8 value)
{
	struct i2c_msg msg;
	u8 data_buf[2];
	int ret=-EINVAL;
	if(!client->adapter)return -ENODEV;
	msg.flags=0;
	msg.len=2;
	msg.buf=&data_buf[0];
	msg.addr=addr;
	data_buf[0]=reg;
	data_buf[1]=value;
	ret = i2c_transfer(client->adapter,&msg,1);
	
//	printk("max9286 write ret: %d|addr 0x%02x ,reg 0x%02x ,value 0x%02x\n",ret,addr,reg,value);

	return ret;
}

static int max9286_read_register(struct i2c_client *client,
		u8 addr, u8 reg, u8 *value)
{
	struct i2c_msg msg[2];
	u8 reg_buf, data_buf;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg_buf;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &data_buf;

	reg_buf = reg;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;
	*value = data_buf;
	printk("the value of address:0x%02x is 0x%02x\n", reg, *value);
	
	return (ret < 0) ? ret : 0;
}

static int isp_32bit_read_register_early(struct i2c_client *client,
		u8 addr, u16 reg, u32 *value)
{
	struct i2c_msg msg[2];
	u8 reg_buf[2], data_buf[4];
	int ret;

	*value = 0;
	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	
	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = &reg_buf[0];
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 4;
	msg[1].buf = &data_buf[0];

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	*value = (data_buf[0] << 24) | (data_buf[1] << 16) |
		(data_buf[2] << 8) | (data_buf[3] ); 

	return (ret < 0) ? ret : 0;
}

static int isp_32bit_read_register(struct i2c_client *client,
		u8 addr, u16 reg, u32 *value)
{

	isp_32bit_read_register_early(client, addr, reg, value);
	printk("File: Line: %05d: the value of ISP 0x%04x is 0x%08x\n",
		   __LINE__, reg, *value);

	return 0;
}
	
static int isp_32bit_write_register(struct i2c_client *client,
		u8 addr, u16 reg, u32 value)
{
	struct i2c_msg msg;
	u8 reg_buf[6];
	u32 tmp_value = 0;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	reg_buf[2] = (value >> 24) &0xFF;
	reg_buf[3] = (value >> 16) &0xFF;
	reg_buf[4] = (value >> 8) &0xFF;
	reg_buf[5] = value &0xFF;
	
	msg.addr = addr;
	msg.flags = 0;
	msg.len = 6;
	msg.buf = &reg_buf[0];

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	isp_32bit_read_register_early(client, addr, reg, &tmp_value);
	if (tmp_value == value)
		printk("File: Successfully set the value of ISP(32 bit) 0x%04x is 0x%08x\n",
			      reg, value);
	else
		printk("File: ISP(32 bit) 0x%04x set ERR: 0x%08x!\n", reg, tmp_value);
	
	return (ret < 0) ? ret : 0;
}

static int isp_16bit_read_register_early(struct i2c_client *client,
		u8 addr, u16 reg, u16 *value)
{
	struct i2c_msg msg[2];
	u8 reg_buf[2], data_buf[2];
	int ret;

	*value = 0;
	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	
	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = &reg_buf[0];
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = &data_buf[0];

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	*value = (data_buf[0] << 8) | (data_buf[1]); 

	return (ret < 0) ? ret : 0;
}

static int isp_16bit_read_register(struct i2c_client *client,
		u8 addr, u16 reg, u16 *value)
{

	isp_16bit_read_register_early(client, addr, reg, value);
	printk("File: Line: %05d: the value of ISP 0x%04x is 0x%04x\n",
		   __LINE__, reg, *value);

	return 0;
}
	
static int isp_16bit_write_register(struct i2c_client *client,
		u8 addr, u16 reg, u16 value)
{
	struct i2c_msg msg;
	u8 reg_buf[4];
	u16 tmp_value = 0;
	int ret;

	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	reg_buf[2] = (value >> 8 ) &0xFF;
	reg_buf[3] = value &0xFF;
	
	msg.addr = addr;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = &reg_buf[0];

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	isp_16bit_read_register_early(client, addr, reg, &tmp_value);
	if (tmp_value == value)
		printk("File: Successfully set the value of ISP(16 bit) 0x%04x is 0x%04x\n",
			      reg, value);
	else
		printk("File: ISP(16 bit) 0x%04x set ERR: 0x%04x!\n", reg, tmp_value);
	
	return (ret < 0) ? ret : 0;
}

static int isp_8bit_read_register_early(struct i2c_client *client,
		u8 addr, u16 reg, u8 *value)
{
	struct i2c_msg msg[2];
	u8 reg_buf[2], data_buf;
	int ret;

	*value = 0;
	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	
	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = &reg_buf[0];
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = &data_buf;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret < 0)
		return ret;

	*value = data_buf;

	return (ret < 0) ? ret : 0;
}

static int isp_8bit_read_register(struct i2c_client *client,
		u8 addr, u16 reg, u8 *value)
{

	isp_8bit_read_register_early(client, addr, reg, value);
	printk("File: Line: %05d:  the value of ISP 0x%04x is 0x%02x\n",
		   __LINE__, reg, *value);

	return 0;
}
	
static int isp_8bit_write_register(struct i2c_client *client,
		u8 addr, u16 reg, u8 value)
{
	struct i2c_msg msg;
	u8 reg_buf[3], tmp_value = 0;
	
	int ret;

	if (!client->adapter)
		return -ENODEV;

	reg_buf[0] = (reg >> 8 ) &0xFF;
	reg_buf[1] = reg &0xFF;
	reg_buf[2] = value ;
	
	msg.addr = addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = &reg_buf[0];

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0)
		return ret;

	isp_8bit_read_register_early(client, addr, reg, &tmp_value);
	if (tmp_value == value)
		printk("File: Successfully set the value of ISP(8 bit) 0x%04x is 0x%02x\n",
			      reg, value);
	else
		printk("File: ISP(8 bit) 0x%04x set ERR: 0x%02x!\n", reg, tmp_value);
	
	return (ret < 0) ? ret : 0;
}

static int isp_initial_setup(struct i2c_client *client)
{
	
// Step5-AWB_CCM
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8DC, 0x10D);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8DE, 0xFFE4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8E0, 0xF  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8E2, 0xFFC3);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8E4, 0x13C);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8E6, 0x1  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8E8, 0xFFF0);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8EA, 0xFFC8);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8EC, 0x148);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC912, 0x53 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC914, 0x1C5);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8EE, 0x118);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8F0, 0xFFC2);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8F2, 0x26 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8F4, 0xFFD3);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8F6, 0x126);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8F8, 0x7  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8FA, 0x15 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8FC, 0xFFCA);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8FE, 0x120);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC916, 0x8F );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC918, 0x182);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC900, 0xF5 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC902, 0x17  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC904, 0xFFF4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC906, 0xFFE0);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC908, 0x112);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC90A, 0xE  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC90C, 0x12 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC90E, 0xFFEC);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC910, 0x101);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC91A, 0x9A );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC91C, 0xB5 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC91E, 0x9C4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC920, 0xD67);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC922, 0x1964);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC924, 0x9C4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC926, 0x1964);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC97D, 0x10 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC92A, 0x20 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC92C, 0x18 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC92E, 0x80 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC930, 0x80 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC932, 0x4  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC934, 0xFFD9);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC936, 0x35 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC937, 0x23 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC938, 0x0  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC93A, 0x17 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC93C, 0x1100);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC93E, 0x1000);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC940, 0x0  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC942, 0x102);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC944, 0x2021);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC946, 0x1000);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC948, 0x0  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC94A, 0x120);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC94C, 0x3430);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC94E, 0x2000);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC950, 0x0  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC952, 0x122);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC954, 0x2000);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC956, 0x100);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC958, 0x110);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC95A, 0x22 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC95C, 0x110);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC95E, 0x2210);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC960, 0x1234);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC962, 0x4540);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC964, 0x112);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC966, 0x3521);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC968, 0x1345);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC96A, 0x6540);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC96C, 0x402);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC96E, 0x3530);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC970, 0x234);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC972, 0x3001);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC974, 0x100);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC976, 0x2200);
	mdelay(20);
	
	
//Step6-CPIPE_Calibration
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC888, 0x80);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9CC, 0xF9C0);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0x3210, 0xEB0);
	mdelay(20);
	
	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC02, 0x3C5);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC844, 0x801);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA802, 0x1C);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xA814, 0x8 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xA81E, 0x8C);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8CE, 0x35);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8BE, 0x0 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA84C, 0x3E6);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA83E, 0x300);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA850, 0x133);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC82E, 0x8 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCC02, 0xF07);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC824, 0x180);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC826, 0x180);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8CA, 0x1A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8CC, 0x80);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC440, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC442, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC444, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC446, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC81A, 0x15);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC81C, 0x80);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC82A, 0x809);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC82C, 0x2E);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC82F, 0x5 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E6, 0x32);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E7, 0x5 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9EA, 0x32);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9EB, 0x8  );
	mdelay(20);
	
	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0x3222, 0x912);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0x3224, 0x612);
	mdelay(20);
	
	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9DC, 0x1F4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9DE, 0xBB8);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9E0, 0xDC0);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9E2, 0x20 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA34, 0x20 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA3C, 0x5A );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA44, 0x16B);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA4C, 0x2D6);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA36, 0x44 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA3E, 0x49 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA46, 0xA3 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA4E, 0x13B);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA38, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA40, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA48, 0xCE );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA50, 0xCE );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAC8, 0x44 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCACC, 0x49 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAD0, 0xA3 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAD4, 0x13B);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAD8, 0x44);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCADC, 0x49 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAE0, 0xA3 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAE4, 0x13B);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCACA, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCACE, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAD2, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAD6, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCADA, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCADE, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAE2, 0x8E );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCAE6, 0x8E );
	isp_32bit_write_register(client, ISP_I2C_ADDR, 0xCAE8, 0x7D0);
	isp_32bit_write_register(client, ISP_I2C_ADDR, 0xCAEC, 0xDAC);
	isp_32bit_write_register(client, ISP_I2C_ADDR, 0xCAF0, 0xC350);
	isp_32bit_write_register(client, ISP_I2C_ADDR, 0xCAF4, 0xF618);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9C8, 0x11 );
	isp_32bit_write_register(client, ISP_I2C_ADDR, 0xC830, 0x24A5);
	mdelay(20);
	

//Step7-CPIPE_Preference
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA2C, 0x3	);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA2E, 0x3	);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA30, 0x3E8);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA32, 0xD00);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9FE, 0x2  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9FF, 0x16 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA04, 0x2  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA05, 0x16 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA0A, 0x14 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA0B, 0x4  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA1A, 0x1  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA1B, 0x16 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA20, 0x1  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA21, 0x16 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA26, 0x1E );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xCA27, 0x5  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9C8, 0x11 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC0A, 0x0  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC0C, 0x4  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC0E, 0x7  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC10, 0xA  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC12, 0xC  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC14, 0xF  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC16, 0x12 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC18, 0x15 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC1A, 0x17 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC1C, 0x1D );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC1E, 0x22 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC20, 0x27 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC22, 0x2C );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC24, 0x36 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC26, 0x40 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC28, 0x49 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC2A, 0x53 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC2C, 0x65 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC2E, 0x77 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC30, 0x89 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC32, 0x9B );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC34, 0xBE );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC36, 0xE0 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC38, 0x101);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC3A, 0x123);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC3C, 0x164);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC3E, 0x1A4);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC40, 0x1E3);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC42, 0x222);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC44, 0x29C);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC46, 0x315);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC48, 0x38B);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBC4A, 0x3FF);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9F0, 0x6E );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9F2, 0x27 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9F3, 0x26 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9F4, 0xFF );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9F5, 0x28	);
	mdelay(20);
	
	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9A6, 0x0   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9A4, 0x0   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC990, 0x16  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC992, 0x3C  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBE, 0xFFFF);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBA, 0x10  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBC, 0x14  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC0, 0xDA  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC2, 0x0   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC4, 0x48  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC98A, 0x80  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC988, 0x37  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC998, 0x320 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC996, 0x320 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC994, 0xAF  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC98A, 0x32  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC434, 0xAF0 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E4, 0x46  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC9F0, 0x6E  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA2C, 0x6   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC82A, 0x3E8 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA70, 0xFB00);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E8, 0x2   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC988, 0x17  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC99A, 0x600 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC99C, 0xB00 );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E8, 0x1	);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC9E9, 0x3	);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA58, 0x900 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA64, 0x400 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA70, 0xFE00);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA60, 0x1CD );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA6C, 0xB3  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA83C, 0x800 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA83E, 0x840 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA840, 0x880 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA842, 0x980 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA844, 0x980 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA846, 0x980 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA848, 0x980 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xA84A, 0x980 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC81C, 0x100 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8C8, 0x400 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC888, 0xA8  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xB00C, 0x60  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8BE, 0x20  );
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xB00D, 0x6   );
	mdelay(20);
	

//Step8-Features
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC88C, 0x0   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xCA9C, 0x285  );
	mdelay(20);
	
	
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xDC07, 0x4   );
	mdelay(20);
	

//AE calibration in Step6-CPIPE_Calibration
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC440, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC442, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC444, 0x71A);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC446, 0x71A);
	mdelay(20);
	
	
//ALTM preference in Step7-CPIPE_Preference	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC416, 0xFE00);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC418, 0x800 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC41E, 0x80 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC420, 0x800 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC412, 0x18);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC414, 0x14);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC41A, 0x32);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC41C, 0x32);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC434, 0xAF0 );
	mdelay(20);
	

//Resetting these registers in Step6-CPIPE_Calibration
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8CE, 0x35);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC82E, 0x8 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC824, 0x180);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC826, 0x180);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC8CC, 0x80);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC81A, 0x15);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC82C, 0x2E);
	isp_8bit_write_register(client, ISP_I2C_ADDR, 0xC82F, 0x5);
	mdelay(20);
	

//Resetting these registers in Step7-CPIPE_Preference	
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBE, 0xFFFF);
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBA, 0x10  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCBC, 0x14  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC0, 0xDA  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC2, 0x0   );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xBCC4, 0x48  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC996, 0x320 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC994, 0xAF  );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC82A, 0x3E8 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC99A, 0x600 );
	isp_16bit_write_register(client, ISP_I2C_ADDR, 0xC99C, 0xB00 );
	mdelay(20);
	
	return 0;
}

#if 0
static int max9286_file_write(int fd, struct i2c_client *client,
							  u8 reg_address, u8 value)
{
	struct i2c_smbus_ioctl_data args;
	union i2c_smbus_data data;
	mm_segment_t old_fs;
	
	int res = -1;
	int value, daddress;

	old_fs = get_fs();
	set_fs(get_ds());

	client->addr = 0x40;
	MYDEBUG("dev-name:%s, addr:0x%02x",
			client->adapter->name, client->addr);

	daddress = 0x04;
	value = 0x43;
	data.byte = value;

	args.read_write = I2C_SMBUS_WRITE;
	args.command = daddress;
	args.size = I2C_SMBUS_BYTE_DATA;
	args.data = &data;
//	res = file->f_op->unlocked_ioctl(file,I2C_SMBUS_MY,&args);
	sys_ioctl(fd,I2C_SMBUS_MY,&args);
	MYDEBUG("value: 0x%02x", res);
	set_fs(old_fs);
	sys_close(fd);
	
	return 0;
}


static int max9286_file_open(struct i2c_client *client)
{
	int fd;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(get_ds());

	fd = sys_open("/dev/i2c-0", O_RDWR, 0644);
	if (IS_ERR(fd)) {
		pr_err("%s(): ERROR opening file(%s) with errno = %ld!\n",
			   __func__, "/dev/i2c-0", PTR_ERR(fd));
		return PTR_ERR(fd);
	}
	set_fs(old_fs);
	
	max9286_file_write(fd, client);

	return 0;
}
#endif

static int  max96705_reset(struct i2c_client *client, int link_num, u8 addr)
{
	u8 link_value = 0;

	link_value = 0xf0|(0x01 << link_num);
	
	max9286_write_register(client, 0x40, 0x04,0x43);
	mdelay(2);
	max9286_write_register(client, 0x40, 0x08,0x81); 
	mdelay(2);

	max9286_write_register(client, MAX9286_I2C_ID, 0x0a, link_value);
	mdelay(2);
	max9286_write_register(client, MAX9286_I2C_ID, 0x03, 0x0f);
	mdelay(2);
	max9286_write_register(client, MAX96705_I2C_ID, 0x00, (addr << 1));
	mdelay(2);
	max96705_initial_setup(client, addr);
	mdelay(2);

	max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xff);
	mdelay(5);
	max9286_write_register(client, addr, 0x07,0x84);
	mdelay(5);
	max9286_write_register(client, addr, 0x04,0x83);
	mdelay(5);
}

static void max96705_handler(struct work_struct *work)
{
	int ret =0;
	u8 value_49 = 0;
	u8 test_value = 0;
	
	
	struct max9286_state *state = NULL;
	state = container_of(work, struct max9286_state, max_work);
	
	struct i2c_client *client = state->client;

	MYDEBUG("client->flags: %d", client->flags);
	client->flags |= I2C_CLIENT_PEC;

	max9286_read_register(client, 0x48, 0x49, &value_49);
//	MYDEBUG("the value of 0x49: 0x%02x", value_49);

	if (value_49 != 0x0f) {
		lock_max9286 = 0;
//		MYDEBUG("Camera out or lack camera!");
		return;
	}

	ret = max9286_read_register(client, 0x41, 0x01, &test_value);
	if(ret < 0)
		max96705_reset(client, 0, 0x41);

	ret = max9286_read_register(client, 0x42, 0x01, &test_value);
	if(ret < 0)
		max96705_reset(client, 1, 0x42);

	ret = max9286_read_register(client, 0x43, 0x01, &test_value);
	if(ret < 0)
		max96705_reset(client, 2, 0x43);

	ret = max9286_read_register(client, 0x46, 0x01, &test_value);
	if(ret < 0)
		max96705_reset(client, 3, 0x46);
	
	mdelay(200);
	lock_max9286 = 0;
//	MYDEBUG("Config , camera LINK!");
}

static irqreturn_t max9286_irq(int irq, void *data)
{
	struct max9286_state *state = data;
	if(lock_max9286 == 0)	{
		lock_max9286++;
 
//		MYDEBUG("IRQ start, time: %d  !", irq_max9286);
 
		schedule_work(&state->max_work);
 
//		MYDEBUG("IRQ OVER, time: %d !", irq_max9286);
		irq_max9286++;
	}	else
//		MYDEBUG("Double interrupt!");
	
	return IRQ_HANDLED;
}

static void max9286_max96705_initial_setup(struct i2c_client *client)
{
	int ret = 0;
	int value = 0;
	
	value = 0x00;

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x1c,0xf4 );	
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x1b,0x0f );	
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x15 ,0x03);
	mdelay(2);
	
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x3f, 0x4f);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x3b, 0x1e);
	mdelay(2);
	
	ret = max9286_write_register(client, 0x40, 0x04,0x43 );
	mdelay(2);
	
	ret = max9286_write_register(client, 0x40, 0x08,0x81 );
	mdelay(2);
	
	ret = max9286_write_register(client, 0x40, 0x97,0x5f );
	mdelay(2);
	
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x3b, 0x19);
	mdelay(2);
	
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0b, 0xe4);
	mdelay(2);
	
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x00, 0x8f);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x01, 0x00);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x08, 0x26);
	mdelay(2);
	
	if(LINK_NUM == 1)
		ret = max9286_write_register(client, MAX9286_I2C_ID, 0x12,0x33 );
	else
		ret = max9286_write_register(client, MAX9286_I2C_ID, 0x12,0xf3 );
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x63,0x00 );
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x64,0x00 );
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x62,0x00 );
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x61,0x00 );
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x5f, 0x0f);
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x06, 0x00);
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x07,0xf2);
	mdelay(2);
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x08,0x2b );
	mdelay(2);

//72Mhz变为84Mhz	
// 	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x06, 0x00);
// 	mdelay(2);
// 	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x07,0x45);
// 	mdelay(2);
// 	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x08,0x33 );
// 	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x2c,0x00);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xf1 );		
	mdelay(2);
	ret = max9286_write_register(client, MAX96705_I2C_ID, 0x00, 0x82);
	mdelay(2);
	max96705_initial_setup(client, 0x41);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xf2 );		
	mdelay(2);
	ret = max9286_write_register(client, MAX96705_I2C_ID, 0x00, 0x84);
	mdelay(2);
	max96705_initial_setup(client, 0x42);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xf4);		
	mdelay(2);
	ret = max9286_write_register(client, MAX96705_I2C_ID, 0x00, 0x86);
	mdelay(2);
	max96705_initial_setup(client, 0x43);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xf8);		
	mdelay(2);
	ret = max9286_write_register(client, MAX96705_I2C_ID, 0x00, 0x8c);
	mdelay(2);
	max96705_initial_setup(client, 0x46);
	mdelay(2);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x0a, 0xff);		
	
	ret = max9286_write_register(client, 0x45, 0x07,0x84 );
	mdelay(5);
	ret = max9286_write_register(client, 0x45, 0x04,0x83 );

	mdelay(5);

	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x41,0x02 );
	
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x32,0x00 );
	ret = max9286_write_register(client, MAX9286_I2C_ID, 0x00,0xef );
	
}

static void max96705_initial_setup(struct i2c_client *client, u8 addr)
{
	int ret = 0;

	max9286_write_register(client, addr, 0x07,0x94);
	mdelay(2);
	max9286_write_register(client, addr, 0x40,0x2f);
	mdelay(2);
	max9286_write_register(client, addr, 0x43,0x25);
	mdelay(2);
	max9286_write_register(client, addr, 0x47,0x2a);
	mdelay(2);
	max9286_write_register(client, addr, 0x48,0x00);
	mdelay(2);
	max9286_write_register(client, addr, 0x49,0x00);
	mdelay(2);
	max9286_write_register(client, addr, 0x4a,0x01);
	mdelay(2);
	max9286_write_register(client, addr, 0x4b,0xf2);
	mdelay(2);
	max9286_write_register(client, addr, 0x4c,0x00);
	mdelay(2);
	
	max9286_write_register(client, addr, 0x20,0x7);
	mdelay(2);											
	max9286_write_register(client, addr, 0x21,0x6);
	mdelay(2);											
	max9286_write_register(client, addr, 0x22,0x5);
	mdelay(2);											
	max9286_write_register(client, addr, 0x23,0x4);
	mdelay(2);											
	max9286_write_register(client, addr, 0x24,0x3);
	mdelay(2);											
	max9286_write_register(client, addr, 0x25,0x2);
	mdelay(2);											
	max9286_write_register(client, addr, 0x26,0x1);
	mdelay(2);											
	max9286_write_register(client, addr, 0x27,0x0);
	mdelay(2);

	max9286_write_register(client, addr, 0x30,0x17);
	mdelay(2);
	max9286_write_register(client, addr, 0x31,0x16);
	mdelay(2);
	max9286_write_register(client, addr, 0x32,0x15);
	mdelay(2);
	max9286_write_register(client, addr, 0x33,0x14);
	mdelay(2);
	max9286_write_register(client, addr, 0x34,0x13);
	mdelay(2);
	max9286_write_register(client, addr, 0x35,0x12);
	mdelay(2);
	max9286_write_register(client, addr, 0x36,0x11);
	mdelay(2);
	max9286_write_register(client, addr, 0x37,0x10);
	mdelay(2);

	max9286_write_register(client, addr, 0x28,0x48);
	mdelay(2);
	max9286_write_register(client, addr, 0x29,0x49);
	mdelay(2);
	max9286_write_register(client, addr, 0x2a,0x4a);
	mdelay(2);
	max9286_write_register(client, addr, 0x2b,0x4b);
	mdelay(2);
	max9286_write_register(client, addr, 0x2c,0x4c);
	mdelay(2);
	max9286_write_register(client, addr, 0x2d,0x4d);
	mdelay(2);
	max9286_write_register(client, addr, 0x2e,0x4e);
	mdelay(2);
	max9286_write_register(client, addr, 0x2f,0x4f);
	mdelay(2);
		

	max9286_write_register(client, addr, 0x38,0x58);
	mdelay(2);
	max9286_write_register(client, addr, 0x39,0x59);
	mdelay(2);
	max9286_write_register(client, addr, 0x3a,0x5a);
	mdelay(2);
	max9286_write_register(client, addr, 0x3b,0x5b);
	mdelay(2);
	max9286_write_register(client, addr, 0x3c,0x5c);
	mdelay(2);
	max9286_write_register(client, addr, 0x3d,0x5d);
	mdelay(2);
	max9286_write_register(client, addr, 0x3e,0x5e);
	mdelay(2);
	max9286_write_register(client, addr, 0x3f,0x0e);
	mdelay(2);
	max9286_write_register(client, addr, 0x0c, (addr << 1));
	mdelay(2);
	max9286_write_register(client, addr, 0x01,0x94 );
	mdelay(5);
	max9286_write_register(client, addr, 0x0b,0x8a);
	mdelay(5);
	max9286_write_register(client, addr, 0x0e,0x32);
	mdelay(5);
}

static int max9286_max96705_initialize(struct i2c_client *client)
{
	struct max9286_max96705_priv *priv = i2c_get_clientdata(client);

	dev_info(&client->dev, "LINKs=%d, LANES=%d, FSYNC mode=%s, FSYNC period=%d, PCLK edge=%s\n",
			 priv->links, priv->lanes, priv->fsync_mode, priv->fsync_period,
			 priv->pclk_rising_edge ? "rising" : "falling");

	max9286_max96705_initial_setup(client);

	client->addr = priv->des_addr;

	return 0;
}

static int max9286_max96705_parse_dt(struct i2c_client *client)
{
	struct max9286_max96705_priv *priv = i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	struct device_node *endpoint = NULL;
	int  i;
	int sensor_delay;
	char fsync_mode_default[20] = "manual"; /* manual, automatic, semi-automatic, external */
	u8 val = 0;
	
	if (of_property_read_u32(np, "maxim,links", &priv->links))
		priv->links = 4;
	
	if (of_property_read_u32(np, "maxim,lanes", &priv->lanes))
		priv->lanes = 4;
	
	reg8_read(client, 0x1e, &val);				/* read max9286 ID */
	if (val != MAX9286_ID)
	{
		MYDEBUG("the val of MAX9286_ID is %d\n", val);
//		return -ENODEV;
	}
	
	if (!of_property_read_u32(np, "maxim,sensor_delay", &sensor_delay))
		mdelay(sensor_delay);
	
	if (of_property_read_string(np, "maxim,fsync-mode", &priv->fsync_mode))
		priv->fsync_mode = fsync_mode_default;
	
	if (of_property_read_u32(np, "maxim,fsync-period", &priv->fsync_period))
		priv->fsync_period = 3200000;			/* 96MHz/30fps */
	priv->pclk_rising_edge = true;
	if (of_property_read_bool(np, "maxim,pclk-falling-edge"))
		priv->pclk_rising_edge = false;
	if (of_property_read_u32(np, "maxim,timeout", &priv->timeout))
		priv->timeout = 100;
	
	for (i = 0; i < 1; i++) {
		endpoint = of_graph_get_next_endpoint(np, endpoint);
		if (!endpoint)
			break;
		
		of_node_put(endpoint);

		if (of_property_read_u32(endpoint, "max96705-addr",&priv->max96705_addr_map[i])) {
			dev_err(&client->dev, "max96705-addr not set\n");
			return -EINVAL;
		}

		priv->sd_of_node[i] = endpoint;
	}
	
	return 0;
}

static int max9286_max96705_probe(struct i2c_client *client,
								  const struct i2c_device_id *id)
{
	struct max9286_max96705_priv *priv;
	struct max9286_state *state;
	struct device *dev = &client->dev;
	int err;
	u16 value;
	
	devm_gpio_request_one(dev,437,GPIOF_OUT_INIT_LOW,GP0);
	devm_gpio_request_one(dev,438,GPIOF_OUT_INIT_LOW,GP1);
	devm_gpio_request_one(dev,445,GPIOF_OUT_INIT_LOW,FSYNC);
	gpio_set_value(437,1);
	gpio_set_value(438,1);
	gpio_set_value(445,1);
 
	devm_gpio_request_one(dev,444,GPIOF_OUT_INIT_LOW,PWDN);
	mdelay(250);
	gpio_set_value(444,1);

	state = kzalloc(sizeof(struct max9286_state), GFP_KERNEL);
	if (state == NULL) {
		return -ENOMEM;
	}

	state->client = client;
	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(client, priv);

	priv->des_addr = client->addr;
	priv->client = client;

	atomic_set(&priv->use_count, 0);
	
	err = max9286_max96705_parse_dt(client);
	if (err)
		goto out;
	
	err = max9286_max96705_initialize(client);
	if (err < 0)
		goto out;
	
	mutex_init(&state->mutex);
	state->curr_norm = V4L2_STD_NTSC;
	state->width = CAMERA_WIDTH;
	state->height = CAMERA_HEIGHT;
	state->field = V4L2_FIELD_NONE;

	v4l2_i2c_subdev_init(&state->sd, client, &max9286_ops);

	state->sd.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->dev		= dev;
	state->pad.flags = MEDIA_PAD_FL_SOURCE;
	state->sd.entity.flags |= MEDIA_ENT_F_ATV_DECODER;
	state->sd.entity.function = MEDIA_ENT_F_ATV_DECODER;
	err = media_entity_pads_init(&state->sd.entity, 1, &state->pad);
	if (err)
		goto out;

	err = v4l2_async_register_subdev(&state->sd);
	if (err)
		goto out;

	isp_16bit_read_register(client, ISP_I2C_ADDR, 0x0000, &value);
	
	state->irq = gpio_to_irq(443);
	if (state->irq < 0){
		MYDEBUG("gpio_to_irq ERR!");
		return -1;
	}
	mdelay(500);

//	max96705_work = create_workqueue("max96705 irq work");
	INIT_WORK(&state->max_work, max96705_handler);  
//	queue_work(max96705_work,&state->max_work);
	
	err = devm_request_irq(dev, state->irq, max9286_irq,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
						   "max9286_lock", state);
	if (err) {
		MYDEBUG("Request IRQ%d failed\n", state->irq);
		return err;
	}
	
	MYDEBUG("9286 probe over!");
	return 0;

out:
	mutex_destroy(&state->mutex);
	kfree(state);
	return err;
}

static int max9286_max96705_remove(struct i2c_client *client)
{
	struct max9286_max96705_priv *priv = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < 4; i++) {
		v4l2_async_unregister_subdev(&priv->sd[i]);
		v4l2_device_unregister_subdev(&priv->sd[i]);
	}

	return 0;
}

static const struct of_device_id max9286_max96705_dt_ids[] = {
	{ .compatible = "maxim,max9286_max96705" },
	{},
};
MODULE_DEVICE_TABLE(of, max9286_max96705_dt_ids);


static const struct i2c_device_id max9286_max96705_id[] = {
	{ "max9286_max96705", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9286_max96705_id);

static struct i2c_driver max9286_max96705_i2c_driver = {
	.driver	= {
		.name		= "max9286_max96705",
		.of_match_table	= of_match_ptr(max9286_max96705_dt_ids),
	},
	.probe		= max9286_max96705_probe,
	.remove		= max9286_max96705_remove,
	.id_table	= max9286_max96705_id,
};

module_i2c_driver(max9286_max96705_i2c_driver);

MODULE_DESCRIPTION("GMSL driver for MAX9286_MAX96705");
MODULE_AUTHOR("DIAS ESW");
MODULE_LICENSE("GPL");


