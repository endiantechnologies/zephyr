/*
 * Copyright (c) 2021 Endian Technologies AB
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <init.h>
#include <zephyr.h>
#include <device.h>
#include <stdio.h>
#include <posix/time.h>
#include <posix/sys/time.h>
#include <sys/printk.h>
#include <drivers/counter.h>
#include <drivers/rtc/pcf8523.h>

void show_clock()
{
	struct timespec tspec = { .tv_sec = 0, .tv_nsec = 0 };
	int res;

	res = clock_gettime(CLOCK_REALTIME, &tspec);
	if (res < 0) {
		printk("Failed to fetch current time\n");
		return;
	}
	struct tm *tm = gmtime(&tspec.tv_sec);

	printk("TIME(utc): %d:%d:%d - %d:%d:%d\n", tm->tm_year + 1900,
	       tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void update_clock(const struct device *rtc)
{
	int ret;
	uint32_t ticks;
	struct timespec tspec = { .tv_sec = 0, .tv_nsec = 0 };

	ret = counter_get_value(rtc, &ticks);
	if (ret == 0) {
		tspec.tv_sec = ticks;
		ret = clock_settime(CLOCK_REALTIME, &tspec);
		printk("Time set from RTC: %u\n", ticks);
	} else {
		printk("RTC clock invalid\n");
	}
}

void main(void)
{
	const struct device *dev;
	const char *dev_id = DT_LABEL(DT_INST(0, nxp_pcf8523));

	dev = device_get_binding(dev_id);

	if (dev == NULL) {
		printk("Device not found! %s\n", dev_id);
		return;
	}

	if (IS_ENABLED(CONFIG_APP_RESET_RTC)) {
		int ret = pcf8523_rtc_set_time(dev, 0);
		if (ret) {
			printk("Failed to update the RTC\n");
			return;
		}
	}

	update_clock(dev);
	printk("Starting the clock\n");

	while (1) {
		show_clock();
		k_sleep(K_SECONDS(1));
	}
}
