#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>

#include "utils.h"
#include "colour.h"
#include "trigger.h"
#include "version.h"

//-----------------------------------------------------------------------------------------------
// Local functiond definitions
//-----------------------------------------------------------------------------------------------
void splash(void);
void init_rp_trigger(Trigger *trigger);
void dinit_rp_trigger(Trigger *trigger);
void load_bitstream(void);
void exit_handler(int sig);

//-----------------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------------
Trigger *trigger;

//-----------------------------------------------------------------------------------------------
// Local function declarations
//-----------------------------------------------------------------------------------------------
void splash(void)
{
	system("clear\n");
	printf("%.*s\n", 80, "================================================================================");
	char info[100];
	sprintf(info, "droneSAR MiloSAR Measurement Control Trigger %s\n\n", MILOSAR_VERSION);
  cprint(info, RESET, GREEN);
	printf("%.*s\n", 80, "================================================================================");
}

void exit_handler(int sig) 
{
	cprint("\n[**] ", BRIGHT, CYAN);
	printf("Exiting Safely.\n");

  dinit_rp_trigger(trigger);

  printf("Shutdown Complete\n");
	exit(sig == -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}

void load_bitstream(void)
{
  cprint("[**] ", BRIGHT, CYAN);
  printf("Loading Bitstream: '%s'\n", "/opt/redpitaya/milosar_trigger/system_wrapper.bit");
  
  // load bitstream
	char cmd[100];
	sprintf(cmd, "cat %s > /dev/xdevcfg\n", "/opt/redpitaya/milosar_trigger/system_wrapper.bit");
	system(cmd);
}

void init_rp_trigger(Trigger *trigger)
{
  cprint("[**] ", BRIGHT, CYAN);
  printf("Init RP Trigger");

	//create memory mappings
	ASSERT(init_mem(), "Failed to open /dev/mem.");

  // init the Trigger Button
  init_trigger(trigger);
}

void dinit_rp_trigger(Trigger *trigger)
{
  cprint("[**] ", BRIGHT, CYAN);
  printf("Dinit RP Trigger");

  // de-initialise the trigger
  dinit_trigger(trigger);

  // dealocate memory mapping
  ASSERT(dnit_mem(), "Failed to deallocate /dev/mem.");
}

//-----------------------------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
  uint8_t capture_count = 0;
  signal(SIGINT, exit_handler);
  signal(SIGTSTP, exit_handler);

  // Display splash screen
  splash();

   while (1)
  {
     // Trigger Button
    trigger = malloc(sizeof(*trigger));

    // #pragma region SpinUp
    // init system
    load_bitstream();
    init_rp_trigger(trigger);

    // start trigger thread
    trigger->state = Unpressed;
    pthread_create(&trigger->thread, NULL, *trigger_worker, (void *)trigger);
    
    cprint("\n[TRIG] ", BRIGHT, GREEN);
    printf("System Armed\n");
    // #pragma endregion SpinUp
    
    // Wait for Trigger button press to start capture
    while(trigger->state != Pressed)
    {
      usleep(100000); // service other threads
    }

    // Tigger button pressed
    // Disable the trigger, while servicing the rest of the main loop
    trigger->reset = 1;

    cprint("[TRIG] ", BRIGHT, GREEN);
    printf("Trigger pressed: %d\n", ++capture_count);

    // #pragma region SpinDown
    // stop the trigger thread
    trigger->state = Disabled;
    pthread_join(trigger->thread, NULL);

    // dinit system
    dinit_rp_trigger(trigger);

    free(trigger);
    trigger = NULL;
    // #pragma endregion SpinDown

    cprint("\n\n[!!] ", BRIGHT, MAGENTA);
    printf("Calling Milosar Application...\n");

    if ( system("/opt/redpitaya/milosar/milosar") != 0 )
      printf("ERROR executing system() command\n");

    cprint("[XX] ", BRIGHT, MAGENTA);
    printf("Milosar Application Done\n\n");
  }
}





// int main(int argc, char *argv[])
// {
//   trigger = malloc(sizeof(*trigger));

//   splash();

//   //mount SD card and load bitstream
//   init_red_pitaya();

//   printf("Enabling Trigger Button\n");
//   trigger->state = Unpressed;
//   pthread_create(&trigger->thread, NULL, *trigger_worker, (void *)trigger);

//   printf("Stating while loop\n");

//   while (1) 
//   {
//     while(trigger->state != Pressed)
//     {
//       printf("Triger state: %d\n", trigger->state);
//       usleep(1000);
//     }

//     printf("Trigger event, starting milosar!\n");

//     // reset the trigger for the next capture
//     trigger->reset =1;

//     if ( system("./opt/redpitaya/milosar/milosar") != 0 )
//     {
//         printf("ERROR executing system() command\n");
//     }

//     // reset the trigger for the next capture
//     trigger->reset =0;
//     printf("Waiting for new trigger!\n");
//   }

//   trigger->state = Disabled;
//   pthread_join(trigger->thread, NULL);

//   return EXIT_SUCCESS;
// }
