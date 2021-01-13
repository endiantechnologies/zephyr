/*
 * Copyright (c) 2021 Endian Technologies AB
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Real-time clock control based on the PCF8523 counter API.
 *
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_RTC_PCF8523_H_
#define ZEPHYR_INCLUDE_DRIVERS_RTC_PCF8523_H_

#include <time.h>

#include <drivers/counter.h>
#include <kernel.h>
#include <zephyr/types.h>
#include <sys/notify.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Set the RTC to a given time.
 *
 * This sets the time on the RTC based on a Unix timestamp. No attempt
 * is made to get good precision as in the maxim_ds3231 driver.
 *
 * @note This function is *supervisor*.
 *
 * @param dev the PCF8523 device pointer.
 *
 * @param ticks Unix timestamp
 *
 * @retval non-negative on success
 */
int pcf8523_rtc_set_time(const struct device *dev, time_t ticks);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_RTC_PCF8523_H_ */
