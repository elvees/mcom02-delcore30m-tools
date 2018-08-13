/*
 * \file
 * \brief paralleltest - Add numbers
 * on Elcore-30M
 *
 * \copyright
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

#include <stdint.h>

/*
 * @core_id - in register R0
 * @data - in register R2
 * @result - in register R4
 */
int start(uint32_t core_id, uint32_t *data, uint32_t *result)
{
	result[core_id] = data[2 * core_id] + data[2 * core_id + 1];

	return 0;
}
