#ifndef AXP_PROT_H
#define AXP_PROT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void axp_i2c_prot_init(void);
void axp_cmd_init(void);
//void axp_basic_sleep_start(void);
//void state_axp2101_task(void *arg);
void axp2101_isCharging_task(void *arg);

// Returns true when the PMU reports the battery is charging.
bool axp2101_is_charging(void);

#ifdef __cplusplus
}
#endif

#endif
