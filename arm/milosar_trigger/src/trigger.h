#ifndef TRIGGER_H
#define TRIGGER_H

#include "constants.h"

enum TriggerState {Unpressed = 0, Pressed = 1, Disabled = 2};

typedef struct Trigger_S
{
  enum TriggerState state;
  pthread_t thread;
  uint8_t reset;
} Trigger;

void *trigger_worker(void *arg);
void init_trigger(Trigger *trigger);
void dinit_trigger(Trigger *trigger);

#endif  // TRIGGER_H