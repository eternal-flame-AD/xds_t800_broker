#include "leds.h"
#include "zephyr/logging/log_core.h"
#include "zephyr/settings/settings.h"

#include <assert.h>
#include <stdatomic.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/shell/shell.h>
#include <zephyr/toolchain.h>

LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

#define DATA_ACTIVITY_LED_OFF_INTERVAL_MS 200

#define SETTINGS_LED_SUBTREE "led"
#define SETTINGS_LED_BRIGHTNESS_SEGMENT "brightness"

const uint8_t brightness_options[] = {100, 75, 50, 4};

static uint8_t brightness_index = 0;

static _Atomic uint8_t led_state = 0;

struct dimmable_led {
  const struct led_dt_spec led;
  const bool valid;
  const bool is_pwm_led;
};

#define DIMMABLE_LED(x)                                                        \
  COND_CODE_1(DT_NODE_EXISTS(x),                                               \
              ((struct dimmable_led){                                          \
                  .led = LED_DT_SPEC_GET(x),                                   \
                  .is_pwm_led = DT_NODE_HAS_COMPAT(DT_PARENT(x), pwm_leds),    \
                  .valid = DT_NODE_HAS_STATUS_OKAY(x)}),                       \
              ((struct dimmable_led){.valid = false}))

static void dimmable_led_set(const struct dimmable_led *led, uint8_t brightness,
                             bool on) {
  if (!led || !led->valid) {
    return;
  }
  if (led->is_pwm_led) {
    led_set_brightness_dt(&led->led, on ? brightness : 0);
  } else {
    (on && (brightness > 0)) ? led_on_dt(&led->led) : led_off_dt(&led->led);
  }
}

const struct dimmable_led central1_led_dev = DIMMABLE_LED(
#if DT_HAS_ALIAS(central1_status_led)
    DT_ALIAS(central1_status_led)
#else
    DT_NODELABEL(led0)
#endif
);
const struct dimmable_led central2_led_dev = DIMMABLE_LED(
#if DT_HAS_ALIAS(central2_status_led)
    DT_ALIAS(central2_status_led)
#else
    DT_NODELABEL(led1)
#endif
);

const struct dimmable_led data_activity_led_dev = DIMMABLE_LED(
#if DT_HAS_ALIAS(data_activity_led)
    DT_ALIAS(data_activity_led)
#else
    DT_NODELABEL(led2)
#endif
);

static const struct dimmable_led power_led = DIMMABLE_LED(
#if DT_HAS_ALIAS(power_led)
    DT_ALIAS(power_led)
#else
    DT_NODELABEL(led3)
#endif
);

static void refresh_leds(void) {
  uint8_t brightness = brightness_options[brightness_index];
  uint8_t val = atomic_load(&led_state);
  dimmable_led_set(&power_led, brightness, (val & (1 << POWER_LED_BIT)));
  dimmable_led_set(&central1_led_dev, brightness,
                   (val & (1 << CENTRAL1_CON_STATUS_LED_BIT)));
  dimmable_led_set(&central2_led_dev, brightness,
                   (val & (1 << CENTRAL2_CONN_STATUS_LED_BIT)));
  // invert data activity led if power led is not present
  dimmable_led_set(
      &data_activity_led_dev, brightness,
      (((val & (1 << DATA_ACTIVITY_LED_BIT)) != 0) ^ !power_led.valid));
}

static void data_activity_led_off_handler(struct k_work *work) {
  atomic_fetch_and(&led_state, ~(1 << DATA_ACTIVITY_LED_BIT));
  refresh_leds();
}

K_WORK_DELAYABLE_DEFINE(data_activity_led_off_work,
                        data_activity_led_off_handler);

void led_data_activity(void) {
  atomic_fetch_or(&led_state, 1 << DATA_ACTIVITY_LED_BIT);
  refresh_leds();
  k_work_reschedule(&data_activity_led_off_work,
                    K_MSEC(DATA_ACTIVITY_LED_OFF_INTERVAL_MS));
}

void led_set_bit(uint8_t bit) {
  atomic_fetch_or(&led_state, 1 << bit);
  refresh_leds();
}

void led_clear_bit(uint8_t bit) {
  atomic_fetch_and(&led_state, ~(1 << bit));
  refresh_leds();
}

void led_brightness_next(void) {
  brightness_index = (brightness_index + 1) % ARRAY_SIZE(brightness_options);
  refresh_leds();
}

static int brightness_cmd_handler(const struct shell *shell, size_t argc,
                                  char **argv) {
  uint8_t index = brightness_index;
  shell_print(shell, "Brightness: %d (#%d)", brightness_options[index], index);
  return 0;
}

static int brightness_save_cmd_handler(const struct shell *shell, size_t argc,
                                       char **argv) {
  uint8_t brightness = brightness_options[brightness_index];
  settings_save_one(SETTINGS_LED_SUBTREE "/" SETTINGS_LED_BRIGHTNESS_SEGMENT,
                    &brightness, sizeof(brightness));
  shell_print(shell, "Brightness saved: %d", brightness);
  return 0;
}

static int brightness_next_cmd_handler(const struct shell *shell, size_t argc,
                                       char **argv) {
  brightness_index = (brightness_index + 1) % ARRAY_SIZE(brightness_options);
  refresh_leds();
  shell_print(shell, "Brightness: %d (#%d)",
              brightness_options[brightness_index], brightness_index);
  return 0;
}

static int leds_cmd_handler(const struct shell *shell, size_t argc,
                            char **argv) {
  uint8_t val = atomic_load(&led_state);
  uint8_t idx = brightness_index;
  uint8_t brightness = brightness_options[idx];
  const char *yes_no[] = {"no", "yes"};
  struct led_and_name {
    const struct dimmable_led *led;
    const char *name;
    const uint8_t bit;
  };
  struct led_and_name leds[] = {
      {&power_led, "Power", POWER_LED_BIT},
      {&central1_led_dev, "Central1", CENTRAL1_CON_STATUS_LED_BIT},
      {&central2_led_dev, "Central2", CENTRAL2_CONN_STATUS_LED_BIT},
      {&data_activity_led_dev, "Data activity", DATA_ACTIVITY_LED_BIT},
  };
  shell_print(shell, "Brightness: %d (#%d)", brightness, idx);
  for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
    shell_print(shell, "%s led: valid: %s, dimmable: %s, active: %s",
                leds[i].name, yes_no[leds[i].led->valid],
                yes_no[leds[i].led->is_pwm_led],
                yes_no[(val & (1 << leds[i].bit))]);
  }
  return 0;
}

static int brightness_settings_set(const char *name, size_t len,
                                   settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  int rc;

  if (settings_name_steq(name, SETTINGS_LED_BRIGHTNESS_SEGMENT, &next) &&
      !next) {
    uint8_t default_brightness;
    if (len != sizeof(default_brightness)) {
      return -EINVAL;
    }

    rc = read_cb(cb_arg, &default_brightness, sizeof(default_brightness));
    if (rc >= 0) {
      // find the index of the default brightness as the smallest index that is
      // greater than or equal to the default brightness
      uint8_t target_brightness_index = 0;
      uint8_t achieved_brightness = brightness_options[0];
      // first find the maximum brightness
      for (size_t i = 0; i < ARRAY_SIZE(brightness_options); i++) {
        if (brightness_options[i] >= achieved_brightness) {
          achieved_brightness = brightness_options[i];
          target_brightness_index = i;
        }
      }
      // then find the dimmest option
      for (size_t i = 0; i < ARRAY_SIZE(brightness_options); i++) {
        uint8_t candidate_brightness = brightness_options[i];
        if (candidate_brightness >= default_brightness &&
            candidate_brightness < achieved_brightness) {
          achieved_brightness = candidate_brightness;
          target_brightness_index = i;
        }
      }
      brightness_index = target_brightness_index;
      refresh_leds();
      LOG_INF("Default brightness: %d, target brightness index: %d",
              default_brightness, target_brightness_index);
      return 0;
    }

    return rc;
  }

  return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(led, SETTINGS_LED_SUBTREE, NULL,
                               brightness_settings_set, NULL, NULL);

SHELL_STATIC_SUBCMD_SET_CREATE(led_brightness_sub,
                               SHELL_CMD(save, NULL, "Save default brightness",
                                         brightness_save_cmd_handler),
                               SHELL_CMD(next, NULL, "Next brightness",
                                         brightness_next_cmd_handler));

SHELL_STATIC_SUBCMD_SET_CREATE(led_sub,
                               SHELL_CMD(brightness, &led_brightness_sub,
                                         "Brightness command",
                                         brightness_cmd_handler));

SHELL_CMD_REGISTER(led, &led_sub, "LED commands", leds_cmd_handler);