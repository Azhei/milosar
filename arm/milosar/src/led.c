#include "led.h"
#include "utils.h"
#include "reg.h"
#include <time.h>

static void *gpio_trigger_led;
uint32_t led_vals;

void *led_worker(void *arg)
{
  Led *led = (Led *)arg;

  while (led->state != Stopped)
  {
    switch (led->state)
    {
    case Off:   bitclear(led_vals, led->led_id); break;
    case On:    bitset(led_vals, led->led_id); break;
    case Blink: bitflip(led_vals, led->led_id); break;
    
    default:    bitclear(led_vals, led->led_id); break;
    }
    set_reg(gpio_trigger_led, led_vals);
    usleep(200000);
  }

  printf("Shutting down LED: %d\n", led->led_id);
  bitclear(led_vals, led->led_id);
  set_reg(gpio_trigger_led, led_vals);

  return NULL;
}

void init_leds(void)
{
  ASSERT(create_map(SREG, MAP_SHARED, &gpio_trigger_led, GPIO_TRIGGER_LED), "Failed to allocate map for gpio trigger led register.");
  set_reg(gpio_trigger_led, LOW); // clear all LEDs
  led_vals = 0;
}

void dinit_leds(void)
{
  set_reg(gpio_trigger_led, LOW); // clear all LEDs
  led_vals = 0;
  ASSERT(destroy_map(SREG, &gpio_trigger_led), "Failed to deallocate gpio_trigger_led memory.");
}