#include "trigger.h"
#include "utils.h"
#include "reg.h"
#include <time.h>
#include "colour.h"

static void *gpio_trigger_switch;

uint8_t debounce(void);

void *trigger_worker(void *arg)
{
  Trigger *trigger = (Trigger *)arg;

  while (trigger->state != Disabled)
  {
    if (debounce())
    {
      cprint("[!!] ", BRIGHT, YELLOW);
      printf("Trigger Button pressed!\n\n");
      trigger->state = Pressed;
    }
    else
      if (trigger->reset)
        trigger->state = Unpressed;
    usleep(10000);
  }

  printf("Shutting down trigger\n");
  trigger->state = Unpressed;
  set_reg(gpio_trigger_switch, HIGH);

  return NULL;
}

void init_trigger(Trigger *trigger)
{
  ASSERT(create_map(SREG, MAP_SHARED, &gpio_trigger_switch, GPIO_TRIGGER_SWITCH), "Failed to allocate map for gpio trigger switch register.");
  set_reg(gpio_trigger_switch, HIGH);
  trigger->state = Unpressed;
}

void dinit_trigger(Trigger *trigger)
{
  set_reg(gpio_trigger_switch, HIGH);
  trigger->state = Unpressed;
  ASSERT(destroy_map(SREG, &gpio_trigger_switch), "Failed to deallocate gpio_trigger_switch memory.");
}

uint8_t debounce(void) {
  static uint16_t state = 0;
  state = (state<<1) | (uint16_t)get_reg(gpio_trigger_switch) | 0xfe00;
  return (state == 0xff00);
}