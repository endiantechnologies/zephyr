/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2018-2019 Foundries.io
 * Copyright (c) 2020 Endian Technologies AB
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Source material for IPSO Voltage object (3316):
 * http://www.openmobilealliance.org/tech/profiles/lwm2m/3316.xml
 */

//#define CONFIG_LWM2M_IPSO_VOLTAGE_OPTIONAL


#define LOG_MODULE_NAME net_ipso_voltage_sensor
#define LOG_LEVEL CONFIG_LWM2M_LOG_LEVEL

#include <logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <stdint.h>
#include <init.h>

#include "lwm2m_object.h"
#include "lwm2m_engine.h"

/* Server resource IDs */
#define VOLTAGE_SENSOR_VALUE_ID				5700
#define VOLTAGE_SENSOR_UNITS_ID				5701
#define VOLTAGE_MIN_MEASURED_VALUE_ID			5601
#define VOLTAGE_MAX_MEASURED_VALUE_ID			5602
#define VOLTAGE_MIN_RANGE_VALUE_ID			5603
#define VOLTAGE_MAX_RANGE_VALUE_ID			5604
#define VOLTAGE_RESET_MIN_MAX_MEASURED_VALUES_ID	5605
#define VOLTAGE_CURRENT_CALIBRATION_ID			5821
#define VOLTAGE_APPLICATION_TYPE_ID			5750

#define VOLTAGE_MAX_ID		9

#define MAX_INSTANCE_COUNT	CONFIG_LWM2M_IPSO_VOLTAGE_INSTANCE_COUNT

#define VOLTAGE_STRING_SHORT	8
#define APPLICATION_TYPE_LENGTH 16

/*
 * Calculate resource instances as follows:
 * start with VOLTAGE_MAX_ID
 * subtract EXEC resources (1)
 */
#define RESOURCE_INSTANCE_COUNT	(VOLTAGE_MAX_ID - 1)

/* resource state variables */
static float32_value_t sensor_value[MAX_INSTANCE_COUNT];
static char units[MAX_INSTANCE_COUNT][VOLTAGE_STRING_SHORT];
static float32_value_t min_measured_value[MAX_INSTANCE_COUNT];
static float32_value_t max_measured_value[MAX_INSTANCE_COUNT];
static float32_value_t min_range_value[MAX_INSTANCE_COUNT];
static float32_value_t max_range_value[MAX_INSTANCE_COUNT];
static float32_value_t current_calibration_value[MAX_INSTANCE_COUNT];
static char application_type_value[MAX_INSTANCE_COUNT][APPLICATION_TYPE_LENGTH];

static struct lwm2m_engine_obj voltage_sensor;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(VOLTAGE_SENSOR_VALUE_ID, R, FLOAT32),
	OBJ_FIELD_DATA(VOLTAGE_SENSOR_UNITS_ID, R_OPT, STRING),
	OBJ_FIELD_DATA(VOLTAGE_MIN_MEASURED_VALUE_ID, R_OPT, FLOAT32),
	OBJ_FIELD_DATA(VOLTAGE_MAX_MEASURED_VALUE_ID, R_OPT, FLOAT32),
	OBJ_FIELD_DATA(VOLTAGE_MIN_RANGE_VALUE_ID, R_OPT, FLOAT32),
	OBJ_FIELD_DATA(VOLTAGE_MAX_RANGE_VALUE_ID, R_OPT, FLOAT32),
	OBJ_FIELD_EXECUTE_OPT(VOLTAGE_RESET_MIN_MAX_MEASURED_VALUES_ID),
	OBJ_FIELD_DATA(VOLTAGE_CURRENT_CALIBRATION_ID, RW_OPT, FLOAT32),
	OBJ_FIELD_DATA(VOLTAGE_APPLICATION_TYPE_ID, RW_OPT, STRING),
};

static struct lwm2m_engine_obj_inst inst[MAX_INSTANCE_COUNT];
static struct lwm2m_engine_res res[MAX_INSTANCE_COUNT][VOLTAGE_MAX_ID];
static struct lwm2m_engine_res_inst
		res_inst[MAX_INSTANCE_COUNT][RESOURCE_INSTANCE_COUNT];

static void update_min_measured(u16_t obj_inst_id, int index)
{
	min_measured_value[index].val1 = sensor_value[index].val1;
	min_measured_value[index].val2 = sensor_value[index].val2;
	NOTIFY_OBSERVER(IPSO_OBJECT_VOLTAGE_ID, obj_inst_id,
			VOLTAGE_MIN_MEASURED_VALUE_ID);
}

static void update_max_measured(u16_t obj_inst_id, int index)
{
	max_measured_value[index].val1 = sensor_value[index].val1;
	max_measured_value[index].val2 = sensor_value[index].val2;
	NOTIFY_OBSERVER(IPSO_OBJECT_VOLTAGE_ID, obj_inst_id,
			VOLTAGE_MAX_MEASURED_VALUE_ID);
}

static int reset_min_max_measured_values_cb(u16_t obj_inst_id)
{
	int i;

	LOG_DBG("RESET MIN/MAX %d", obj_inst_id);
	for (i = 0; i < MAX_INSTANCE_COUNT; i++) {
		if (inst[i].obj && inst[i].obj_inst_id == obj_inst_id) {
			update_min_measured(obj_inst_id, i);
			update_max_measured(obj_inst_id, i);
			return 0;
		}
	}

	return -ENOENT;
}

static int sensor_value_write_cb(u16_t obj_inst_id,
				 u16_t res_id, u16_t res_inst_id,
				 u8_t *data, u16_t data_len,
				 bool last_block, size_t total_size)
{
	int i;
	bool update_min = false;
	bool update_max = false;

	for (i = 0; i < MAX_INSTANCE_COUNT; i++) {
		if (inst[i].obj && inst[i].obj_inst_id == obj_inst_id) {
			/* update min / max */
			if (sensor_value[i].val1 < min_measured_value[i].val1) {
				update_min = true;
			} else if (sensor_value[i].val1 ==
					min_measured_value[i].val1 &&
				   sensor_value[i].val2 <
					min_measured_value[i].val2) {
				update_min = true;
			}

			if (sensor_value[i].val1 > max_measured_value[i].val1) {
				update_max = true;
			} else if (sensor_value[i].val1 ==
					max_measured_value[i].val1 &&
				   sensor_value[i].val2 >
					max_measured_value[i].val2) {
				update_max = true;
			}

			if (update_min) {
				update_min_measured(obj_inst_id, i);
			}

			if (update_max) {
				update_max_measured(obj_inst_id, i);
			}
		}
	}

	return 0;
}

static struct lwm2m_engine_obj_inst *voltage_sensor_create(u16_t obj_inst_id)
{
	int index, i = 0, j = 0;

	/* Check that there is no other instance with this ID */
	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (inst[index].obj && inst[index].obj_inst_id == obj_inst_id) {
			LOG_ERR("Can not create instance - "
				"already existing: %u", obj_inst_id);
			return NULL;
		}
	}

	for (index = 0; index < MAX_INSTANCE_COUNT; index++) {
		if (!inst[index].obj) {
			break;
		}
	}

	if (index >= MAX_INSTANCE_COUNT) {
		LOG_ERR("Can not create instance - no more room: %u",
			obj_inst_id);
		return NULL;
	}

	/* Set default values */
	sensor_value[index].val1 = 0;
	sensor_value[index].val2 = 0;
	units[index][0] = '\0';
	min_measured_value[index].val1 = INT32_MAX;
	min_measured_value[index].val2 = 0;
	max_measured_value[index].val1 = -INT32_MAX;
	max_measured_value[index].val2 = 0;
	min_range_value[index].val1 = 0;
	min_range_value[index].val2 = 0;
	max_range_value[index].val1 = 0;
	max_range_value[index].val2 = 0;
	current_calibration_value[index].val1 = 0;
	current_calibration_value[index].val2 = 0;
	application_type_value[index][0] = '\0';

	(void)memset(res[index], 0,
		     sizeof(res[index][0]) * ARRAY_SIZE(res[index]));
	init_res_instance(res_inst[index], ARRAY_SIZE(res_inst[index]));

	/* initialize instance resource data */
	INIT_OBJ_RES(VOLTAGE_SENSOR_VALUE_ID, res[index], i,
		     res_inst[index], j, 1, true,
		     &sensor_value[index], sizeof(*sensor_value),
		     NULL, NULL, sensor_value_write_cb, NULL);
	INIT_OBJ_RES_DATA(VOLTAGE_SENSOR_UNITS_ID, res[index], i, res_inst[index], j,
			  units[index], VOLTAGE_STRING_SHORT);
	INIT_OBJ_RES_DATA(VOLTAGE_MIN_MEASURED_VALUE_ID, res[index], i,
			  res_inst[index], j, &min_measured_value[index],
			  sizeof(*min_measured_value));
	INIT_OBJ_RES_DATA(VOLTAGE_MAX_MEASURED_VALUE_ID, res[index], i,
			  res_inst[index], j, &max_measured_value[index],
			  sizeof(*max_measured_value));
	INIT_OBJ_RES_DATA(VOLTAGE_MIN_RANGE_VALUE_ID, res[index], i,
			  res_inst[index], j, &min_range_value[index],
			  sizeof(*min_range_value));
	INIT_OBJ_RES_DATA(VOLTAGE_MAX_RANGE_VALUE_ID, res[index], i,
			  res_inst[index], j, &max_range_value[index],
			  sizeof(*max_range_value));
	INIT_OBJ_RES_EXECUTE(VOLTAGE_RESET_MIN_MAX_MEASURED_VALUES_ID,
			     res[index], i, reset_min_max_measured_values_cb);
	INIT_OBJ_RES_DATA(VOLTAGE_CURRENT_CALIBRATION_ID, res[index], i,
			  res_inst[index], j, &current_calibration_value[index],
			  sizeof(*current_calibration_value));
	INIT_OBJ_RES_DATA(VOLTAGE_APPLICATION_TYPE_ID, res[index], i, res_inst[index], j,
			  application_type_value[index], APPLICATION_TYPE_LENGTH);

	inst[index].resources = res[index];
	inst[index].resource_count = i;
	LOG_DBG("Create IPSO Voltage Sensor instance: %d", obj_inst_id);
	return &inst[index];
}

static int ipso_voltage_sensor_init(struct device *dev)
{
	voltage_sensor.obj_id = IPSO_OBJECT_VOLTAGE_ID;
	voltage_sensor.fields = fields;
	voltage_sensor.field_count = ARRAY_SIZE(fields);
	voltage_sensor.max_instance_count = MAX_INSTANCE_COUNT;
	voltage_sensor.create_cb = voltage_sensor_create;
	lwm2m_register_obj(&voltage_sensor);

	return 0;
}

SYS_INIT(ipso_voltage_sensor_init, APPLICATION,
	 CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
