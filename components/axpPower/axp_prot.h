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

// Returns true when the PMU reports the battery is discharging.
bool axp2101_is_discharging(void);

// Returns 0-100 on success, or -1 if unknown/unavailable.
int axp2101_get_battery_percent(void);

// Lightweight PMU telemetry helpers (best-effort; return 0/false on failure).
// Units: millivolts.
int axp2101_get_batt_voltage_mv(void);
int axp2101_get_vbus_voltage_mv(void);
int axp2101_get_sys_voltage_mv(void);

bool axp2101_is_vbus_in(void);
bool axp2101_is_vbus_good(void);

// Raw charger status enum as returned by XPowersLib.
// (Kept numeric here to avoid coupling callers to XPowers headers.)
int axp2101_get_charger_status(void);

#ifdef __cplusplus
}
#endif

#endif
