/*
 * Copyright (c) 2021 Endian Technologies AB
 * Partly based on counter_cmos.c:
 * Copyright (c) 2019 Intel Corp.
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_LEVEL CONFIG_RTC_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(pcf8523_rtc);

#define DT_DRV_COMPAT nxp_pcf8523

#include <drivers/counter.h>
#include <device.h>
#include <drivers/i2c.h>
#include <time.h>

#define SETTLE_TIME_MS 10

#define REG_CONTROL_1 0x00
#define REG_CONTROL_1_CAP_SEL (1 << 7)
#define REG_CONTROL_3 0x02
#define REG_CONTROL_3_PM_MASK 0xE0
#define REG_SECONDS 0x03
#define REG_SECONDS_OS_FLAG 0x80
#define REG_MINUTES 0x04
#define REG_HOURS 0x05
#define REG_DAYS 0x06
#define REG_WEEKDAYS 0x07
#define REG_MONTHS 0x08
#define REG_YEARS 0x09

#define BIN2BCD(value) (((value) / 10) << 4) + ((value) % 10)
#define BCD2BIN(value, mask) ((((value) & (mask)) >> 4) * 10) + ((value)&0x0F)

enum capacitor_select {
	PF7 = 0,
	PF12_5 = 1,
};

enum power_mode {
	PM0 = 0x00,
	PM1 = 0x01,
	PM2 = 0x02,
	PM3 = 0x04,
	PM4 = 0x05,
	PM5 = 0x07,
};

struct pcf8523_rtc_config {
	struct counter_config_info generic;
	const char *bus_name;
	uint16_t addr;
};

struct pcf8523_rtc_data {
	const struct device *i2c;
};

static int pcf8523_write(const struct device *i2c, uint8_t reg, uint8_t value)
{
	return i2c_reg_write_byte(i2c, DT_INST_REG_ADDR(0), reg, value);
}

static int pcf8523_read(const struct device *i2c, uint8_t reg, uint8_t *value)
{
	return i2c_reg_read_byte(i2c, DT_INST_REG_ADDR(0), reg, value);
}

static int pcf8523_select_capacitance(const struct device *i2c,
				      enum capacitor_select capacitor)
{
	int err = 0;
	uint8_t register_value;

	err = pcf8523_read(i2c, REG_CONTROL_1, &register_value);
	if (err) {
		return err;
	}

	if (capacitor == PF12_5) {
		register_value |= REG_CONTROL_1_CAP_SEL;
	} else {
		register_value &= ~REG_CONTROL_1_CAP_SEL;
	}

	return pcf8523_write(i2c, REG_CONTROL_1, register_value);
}

static int pcf8523_set_power_mode(const struct device *i2c, enum power_mode pm)
{
	int err = 0;
	uint8_t register_value;

	err = pcf8523_read(i2c, REG_CONTROL_3, &register_value);
	if (err) {
		return err;
	}

	register_value = (register_value & ~REG_CONTROL_3_PM_MASK) | (pm << 5);

	return pcf8523_write(i2c, REG_CONTROL_3, register_value);
}

/*
 * Hinnant's algorithm to calculate the number of days offset from the epoch.
 */

static uint32_t hinnant(int y, int m, int d)
{
	unsigned yoe;
	unsigned doy;
	unsigned doe;
	int era;

	y -= (m <= 2);
	era = ((y >= 0) ? y : (y - 399)) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2)/5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return era * 146097 + ((int) doe) - 719468;
}

static int pcf8523_rtc_init(const struct device *dev)
{
	int err;
	struct pcf8523_rtc_data *data = dev->data;

	data->i2c = device_get_binding(DT_INST_BUS_LABEL(0));
	if (!data->i2c) {
		LOG_ERR("Failed to find I2C bus");
		return -ENODEV;
	}

	k_msleep(SETTLE_TIME_MS);

#if DT_INST_NODE_HAS_PROP(0, pf)
	err = pcf8523_select_capacitance(data->i2c, DT_INST_PROP(0, pf));
	if (err) {
		LOG_ERR("RTC capacitance selection failed");
		return err;
	}
#endif

#if DT_INST_NODE_HAS_PROP(0, pm)
	err = pcf8523_set_power_mode(data->i2c, DT_INST_PROP(0, pm));
	if (err) {
		LOG_ERR("RTC power mode selection failed");
		return err;
	}
#endif

	return 0;
}

int pcf8523_rtc_set_time(const struct device *dev, time_t timestamp)
{
	int err = 0;
	struct pcf8523_rtc_data *data = dev->data;
	struct tm date;

	gmtime_r(&timestamp, &date);

	uint8_t regs[7] = {
		BIN2BCD(date.tm_sec),
		BIN2BCD(date.tm_min),
		BIN2BCD(date.tm_hour),
		BIN2BCD(date.tm_mday),
		BIN2BCD(date.tm_wday),
		BIN2BCD(date.tm_mon + 1),
		BIN2BCD(date.tm_year - 100),
	};

	err = i2c_burst_write(data->i2c, DT_INST_REG_ADDR(0), REG_SECONDS,
			      regs, sizeof regs);
	if (err) {
		LOG_ERR("Failed to set RTC time");
	}

	return err;
}

static int pcf8523_rtc_get_value(const struct device *dev, uint32_t *ticks)
{
	int err = 0;
	struct pcf8523_rtc_data *data = dev->data;
	struct tm date;
	uint8_t regs[7];
	uint32_t epoch;

	err = i2c_burst_read(data->i2c, DT_INST_REG_ADDR(0), REG_SECONDS,
			     regs, sizeof regs);
	if (err) {
		LOG_ERR("Unable to read RTC time: %d", err);
		return err;
	}

	if (regs[0] & REG_SECONDS_OS_FLAG) {
		LOG_ERR("RTC time invalid");
		return -EINVAL;
	}

	date.tm_sec = BCD2BIN(regs[0], 0x70);
	date.tm_min = BCD2BIN(regs[1], 0x70);
	date.tm_hour = BCD2BIN(regs[2], 0x30);
	date.tm_mday = BCD2BIN(regs[3], 0x30);
	date.tm_wday = BCD2BIN(regs[4] & 0x07, 0x00);
	date.tm_mon = BCD2BIN(regs[5], 0x10) - 1;
	date.tm_year = BCD2BIN(regs[6], 0xF0) + 100;

	/* Ignore time zones; the RTC runs in UTC. */
	epoch = hinnant(date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
	epoch *= 86400;		      /* seconds per day */
	epoch += date.tm_hour * 3600; /* seconds per hour */
	epoch += date.tm_min * 60; /* seconds per minute */
	epoch += date.tm_sec;

	*ticks = epoch;

	return 0;
}

static const struct counter_driver_api pcf8523_rtc_driver_api = {
	.get_value = pcf8523_rtc_get_value,
};

static struct pcf8523_rtc_data pcf8523_rtc_data_0;

static struct pcf8523_rtc_config pcf8523_rtc_config_0 = {
	.generic = {
		.max_top_value = UINT_MAX,
		.freq = 1
	},
	.bus_name = DT_INST_BUS_LABEL(0),
	.addr = DT_INST_REG_ADDR(0),
};

#if CONFIG_COUNTER_PCF8523_INIT_PRIORITY <= CONFIG_I2C_INIT_PRIORITY
#error COUNTER_PCF8523_INIT_PRIORITY must be greater than I2C_INIT_PRIORITY
#endif

DEVICE_AND_API_INIT(pcf8523_rtc_0, DT_INST_LABEL(0),
		    pcf8523_rtc_init, &pcf8523_rtc_data_0,
		    &pcf8523_rtc_config_0.generic,
		    POST_KERNEL, CONFIG_COUNTER_PCF8523_INIT_PRIORITY,
		    &pcf8523_rtc_driver_api);
