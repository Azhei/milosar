#ifndef LED_H
#define LED_H

#include "constants.h"

enum LedState {Off = 0, On = 1, Blink=2, Stopped = 3};

typedef struct Led_S
{
  enum LedState state;
  int led_id;
  pthread_t thread;
} Led;

void *led_worker(void *arg);
void init_leds(void);
void dinit_leds(void);

#endif  // LED_H