#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include "reg.h"
#include "utils.h"
#include "constants.h"
#include "synth.h"
#include "gps.h"
#include "led.h"
#include "trigger.h"
#include "version.h"

//-----------------------------------------------------------------------------------------------
// Local functiond definitions
//-----------------------------------------------------------------------------------------------
void *record(void *arg);
void init_red_pitaya(void);
void splash(void);
int parse_setup_file(void* pointer, const char* section, const char* attribute, const char* value);
void waitFor (unsigned int secs);

//-----------------------------------------------------------------------------------------------
// Global variables
//-----------------------------------------------------------------------------------------------
Channel *A, *B;
Gps *gps;
Synthesizer tx_synth, lo_synth;
Configuration config;
Led *power_led;
Led *armed_led;
Led *capture_led;

static void *reg_integration, *reg_index, *reg_channel_a_phase_inc, *reg_gpio, *reg_tcu, *reg_channel_b_phase_inc;

//-----------------------------------------------------------------------------------------------
// Local function declarations
//-----------------------------------------------------------------------------------------------
void exit_handler(int sig) 
{
	cprint("\n[**] ", BRIGHT, CYAN);
	printf("Exiting Safely.\n");

	ASSERT(dnit_mem(), "Failed to deallocate /dev/mem.");
	ASSERT(destroy_map(SREG, &reg_integration), "Failed to deallocate reg_integration memory.");
	ASSERT(destroy_map(SREG, &reg_index), "Failed to deallocate reg_index memory.");
	ASSERT(destroy_map(SREG, &reg_channel_a_phase_inc), "Failed to deallocate reg_channel_a_phase_inc memory.");
	ASSERT(destroy_map(SREG, &reg_gpio), "Failed to deallocate reg_gpio memory.");
	ASSERT(destroy_map(SREG, &reg_tcu), "Failed to deallocate reg_tcu memory.");
	ASSERT(destroy_map(SREG, &reg_channel_b_phase_inc), "Failed to deallocate reg_channel_b_phase_inc memory.");

	if (config.is_gpsd) dinit_gps(gps);

  if (config.is_status_leds) dinit_leds();

  printf("Done\n");
	exit(sig == -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}

void waitFor (unsigned int secs) {
    unsigned int retTime = time(0) + secs;   // Get finishing time.
    while (time(0) < retTime);               // Loop until it arrives.
}

//-----------------------------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------------------------
int main(int argc, char **argv)
{
	signal(SIGINT, exit_handler);
  signal(SIGTSTP, exit_handler);
	
	//initialise default values
	tx_synth.id = 0;
	lo_synth.id = 1;
	tx_synth.parameter_file = false;
	lo_synth.parameter_file = false;
	config.storage_dir = SD_STORAGE_DIR;
	config.is_debug = false;
	config.is_data_transfer = false;
	config.is_gpsd = false;
  config.capture_delay = 0;
	gps = malloc(sizeof(*gps));
  
  // status LEDs
  config.is_status_leds = false;
  power_led = malloc(sizeof(*power_led));     
  armed_led = malloc(sizeof(*armed_led));     
  capture_led = malloc(sizeof(*capture_led)); 


	splash();

	//parse configuration options from setup.ini
	if (ini_parse(SETUP_FILE, parse_setup_file, NULL) < 0) 
	{
		ASSERT(FAIL, "Could not open Setup .ini file.\n");
		exit(EXIT_FAILURE);
  }

  // Add time delay here for Scarborough trials
  if (config.capture_delay > 0)
  {
    cprint("[**] ", BRIGHT, CYAN);
    printf("Capture delay starting: %d [s]...\n", config.capture_delay);
    fflush(stdout);
    waitFor(config.capture_delay);
    printf("Capture delay done!...\n");
    fflush(stdout); 
  }

	//mount SD card and load bitstream
	init_red_pitaya();

  // launch the LED worker threads
  if (config.is_status_leds)
  {
    printf("Enabling LEDs\n");
    power_led->state = On;
    armed_led->state = Off;
    capture_led->state = Off;
    pthread_create(&power_led->thread, NULL, *led_worker, (void *)power_led);
    pthread_create(&armed_led->thread, NULL, *led_worker, (void *)armed_led);
    pthread_create(&capture_led->thread, NULL, *led_worker, (void *)capture_led);
  }

  //-----------------------------------------------------------------------------------------------
  // This is the main application loop, allowing for multiple captures using the same configuration
  //-----------------------------------------------------------------------------------------------

    //launch the gps worker thread
    if (config.is_gpsd)
    {
      gps->state = Idle;
      pthread_create(&gps->thread, NULL, *gps_worker, (void *)gps);
    }

    //parse synth ramp parameters from .ini files
    parse_ramp_file(&tx_synth);
    parse_ramp_file(&lo_synth);

    //calculate additional ramp parameters
    calc_parameters(&tx_synth, &config);
    calc_parameters(&lo_synth, &config);

    //import synth register values from template file
    load_registers(SYNTH_REG_TEMP_DIR, &tx_synth);
    load_registers(SYNTH_REG_TEMP_DIR, &lo_synth);

    //wait here for gps fix
    if (config.is_gpsd) wait_for_fix(gps);

    // Indicate that the system is armed and ready for trigger
    if (config.is_status_leds)
      armed_led->state = On;

    // Set the Capture Status LED
    if (config.is_status_leds)
    {
      if (config.capture_delay > 0)
        capture_led->state = Blink;
      else
        capture_led->state = On;

      // switch off the Status Armed LED when capture starts    
      armed_led->state = Off;
    }

    //get user input for final experiment settings
    config_experiment(&config, &tx_synth, &lo_synth, gps);

    //set all gpio pins low
    set_reg(reg_gpio, LOW);

    //init synth pins
    init_pins(&tx_synth);
    init_pins(&lo_synth);

    init_channel(&A, 'A', DMA_A_BASE_ADDR, STS_A_BASE_ADDR);
    pthread_create(&A->thread, NULL, record, (void *)A);

    //software reset the synths
    reset_synths(reg_gpio, &tx_synth, &lo_synth);

    //write to the synth registers
    flash_synth(reg_gpio, &tx_synth);
    flash_synth(reg_gpio, &lo_synth);

    //now that synth parameters have been set
    set_ramping(reg_gpio, &tx_synth, &lo_synth, true);

    //enable gps data recording
    if (config.is_gpsd) gps->state = Active;

    //enable recording and trigger synths in parallel
    time_t tcu_trigger_time = start_experiment(reg_gpio, reg_tcu, &config);

    //wait for threads to finish their work
    pthread_join(A->thread, NULL);

    //clear the enable flag
    set_reg(reg_tcu, LOW);

    //disable ramping once experiment is over
    set_ramping(reg_gpio, &tx_synth, &lo_synth, false);

    //disable gps data recording
    if (config.is_gpsd)
    {
      gps->experiment_dir = config.experiment_dir;
      gps->state= Shutdown;	
      pthread_join(gps->thread, NULL);
    }

    // Update the summary file with the TCU trigger time
    FILE* f;
    f = fopen(config.path_summary, "a");
    //Check if file correctly opened
    if (f == 0)
    {
      cprint("[!!] ", BRIGHT, RED);
      printf("Could not open summary file to update TCU trigger time. Ensure you have read-write access\n");
    }
    else
    {
      //update summary file 
      fprintf(f, "\n[TCU]\r\n");
      struct tm* tm_info  = localtime(&tcu_trigger_time);
      char tcu_trigger_time_str[20];
      strftime(tcu_trigger_time_str, 20, "%y_%m_%d_%H_%M_%S", tm_info);
      fprintf(f, "tcu_trigger_timestamp		= %s\r\n", tcu_trigger_time_str);
      fclose(f);
    }

    if (config.is_data_transfer)
    {
      cprint("[**] ", BRIGHT, CYAN);
      printf("Copying Measurement Data to Host:\n");

      //copy experiment folder from red pitaya to host computer
      char command[250];
      sprintf(command, "scp -r %s/%s %s@%s:%s", config.storage_dir, config.time_stamp, config.host_name, config.host_ip, config.host_dir);
      system(command);
    }
    else
    {
      cprint("[!!] ", BRIGHT, YELLOW);
      printf("Auto Data Transfer Disabled.\n");
    }

    cprint("\n[OK] ", BRIGHT, GREEN);
    printf("Measurement Complete: %s\n", config.time_stamp);

    // reset some parameters for the next loop
    if (config.is_status_leds)
    {
      armed_led->state = Off;
      capture_led->state = Off;
    }

  //-----------------------------------------------------------------------------------------------
  // End of the main application loop. Join all threads
  //-----------------------------------------------------------------------------------------------

  cprint("\n[!!] ", BRIGHT, CYAN);
  printf("Shutting down milosar\n\n");

  // stop the status LED thread
  if (config.is_status_leds)
  {
    power_led->state = Stopped;
    capture_led->state = Stopped;
    armed_led->state = Stopped;
    pthread_join(power_led->thread, NULL);
    pthread_join(armed_led->thread, NULL);
    pthread_join(capture_led->thread, NULL);
  }	

  cprint("\n[**] ", BRIGHT, CYAN);
  printf("Shutdown Complete.\n\n");
	return EXIT_SUCCESS;
}

void *record(void *arg)
{
	Channel *channel = (Channel *)arg;

	ASSERT(create_map(SREG, MAP_SHARED, &channel->sts, channel->sts_base), "Failed to allocate map for STS register.");
	ASSERT(create_map(S4MB, MAP_SHARED, &channel->dma, channel->dma_base), "Failed to allocate map for DMA RAM.");

	//clear fpga buffer
	memset(channel->dma, 0x0, S4MB);

	int position, limit, offset;

	char *path = malloc(strlen(config.experiment_dir) + strlen(config.time_stamp) + 1 + 4);
	strcpy(path, config.experiment_dir);
	strcat(path, config.time_stamp);
	strcat(path, ".bin");

	FILE *f = fopen(path, "w");
	limit = S2MB;

	void *buf;

	if (!(buf = malloc(S2MB)))
	{
		fprintf(stderr, "no memory for temp buffer\n");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < config.n_buffers;)
	{
		//get location of the DMA writer in terms of number of bytes written.
		//fpga ram writer only writes in 32 bit chunks, so each pointer location represents 4 bytes.
		position = get_reg(channel->sts) * BYTES_PER_WRITE;

		//safe to read bottom                 //safe to read top
		if ((limit > 0 && position > limit) || (limit == 0 && position < S2MB))
		{
			offset = limit > 0 ? 0 : S2MB;
			limit = limit > 0 ? 0 : S2MB;

			//copy data from fpga buffer to cpu ram
			memcpy(buf, channel->dma + offset, S2MB);

			//write data from cpu ram to sd card
			fwrite(buf, 1, S2MB, f);

			i++;

			cprint("\033[A\033[J[**] ", BRIGHT, CYAN);	
			printf("%i/%i MB (%3.0f %%)\n", 2*i, 2*config.n_buffers, (float)(i*100.0/config.n_buffers));
		}
	}

	fclose(f);
	free(path);
	free(buf);

	return EXIT_SUCCESS;
}


int parse_setup_file(void* pointer, const char* section, const char* attribute, const char* value)
{
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(attribute, n) == 0
	
	if (MATCH("misc", "debug")) config.is_debug = atoi(value);
	if (MATCH("misc", "enable_transfer")) config.is_data_transfer = atoi(value);
	if (MATCH("misc", "hostname")) config.host_name = strdup(value);
	if (MATCH("misc", "host_ip")) config.host_ip = strdup(value);	
	if (MATCH("misc", "directory")) config.host_dir = strdup(value);
	if (MATCH("misc", "capture_delay")) config.capture_delay = atoi(value);
	if (MATCH("misc", "enable_status_leds")) config.is_status_leds = atoi(value);

	if (MATCH("files", "bitstream")) config.bitstream = strdup(value);	
	if (MATCH("files", "tx_synthesizer")) tx_synth.parameter_file = strdup(value);
	if (MATCH("files", "dx_synthesizer")) lo_synth.parameter_file = strdup(value);

	if (MATCH("timing", "switch_mode")) config.switch_mode = atoi(value);
	if (MATCH("timing", "n_seconds")) config.n_seconds = atoi(value);
	if (MATCH("timing", "prf")) config.prf = atoi(value);
	if (MATCH("timing", "channel_a_phase_increment")) config.channel_a_phase_increment = atoi(value);
	if (MATCH("timing", "channel_b_phase_increment")) config.channel_b_phase_increment = atoi(value);

	if (MATCH("gpsd", "enabled")) config.is_gpsd = atoi(value);
	if (MATCH("gpsd", "min_mode")) gps->min_mode = atoi(value);
	if (MATCH("gpsd", "min_sats")) gps->min_sats = atoi(value);

	if (MATCH("sampling", "decimation_factor")) config.decimation_factor = atoi(value);
	if (MATCH("sampling", "presum_factor")) config.presum_factor = atoi(value);
	if (MATCH("sampling", "start_index")) config.start_index = atoi(value);
	if (MATCH("sampling", "end_index")) config.end_index = atoi(value);

	return 1;	//TODO: Improve error handling.
}


void init_red_pitaya(void)
{
  if (config.is_debug)
	{
		cprint("[**] ", BRIGHT, CYAN);
		printf("Init RP\n");
	}

	// check if SD card has been mounted
	if (system("mount | grep \"/media/storage\" >/dev/null") != 0)
	{
		if (config.is_debug)
		{
			cprint("[**] ", BRIGHT, CYAN);
			printf("Mounting storage partition\n");
		}
		system("mount /dev/mmcblk0p3 /media/storage\n");
	}

	cprint("[OK] ", BRIGHT, GREEN);
	printf("Filesystem Details:\n");
	system("df -T -h /media/storage/");
	printf("\n");

	if (config.is_debug)
	{
		cprint("[**] ", BRIGHT, CYAN);
		printf("Loading Bitstream:\n%s\n", config.bitstream);
	}

	// load bitstream
	char cmd[100];
	sprintf(cmd, "cat %s > /dev/xdevcfg\n", config.bitstream);
	system(cmd);

	//increase program priority 
	setpriority(PRIO_PROCESS, 0, -20);

	//close unnecessary applications
	system("pkill nginx\n");

	//create memory mappings
	ASSERT(init_mem(), "Failed to open /dev/mem.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_integration, INT_BASE_ADDR), "Failed to allocate map for integration register.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_index, INDX_BASE_ADDR), "Failed to allocate map for indexing register.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_channel_a_phase_inc, MAIN_LO_BASE_ADDR), "Failed to allocate map for reference phase increment register.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_gpio, GPIO_BASE_ADDR), "Failed to allocate map for gpio register.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_tcu, TCU_BASE_ADDR), "Failed to allocate map for tcu register.");
	ASSERT(create_map(SREG, MAP_SHARED, &reg_channel_b_phase_inc, REF_LO_BASE_ADDR), "Failed to allocate map for cancellation phase increment register.");

	//set dds phase increment for main channel local oscillator
	set_reg(reg_channel_a_phase_inc, config.channel_a_phase_increment);

	//set dds phase increment for reference channel local oscillator
	set_reg(reg_channel_b_phase_inc, config.channel_b_phase_increment);

	//set chunk start and stop index
	//note that indexing starts from 1!
	uint32_t chunk_index = (config.start_index << 0) + (config.end_index << 16);
	set_reg(reg_index, chunk_index);

	//set number of samples in chunk and integration factor
	config.n_samples_per_pri = floor(1.0/config.prf * ADC_RATE/config.decimation_factor);

	if (config.n_samples_per_pri >= FIFO_DEPTH) 
	{
		printf("Number of samples in PRI exceeds FIFO depth!\n");
		exit(0);
	}

	uint32_t integration = (config.n_samples_per_pri << 0) + (config.presum_factor << 16);
	set_reg(reg_integration, integration);

  // init the Status LEDs
  init_leds();
  power_led->led_id = GPIO_POWER_LED;
  armed_led->led_id = GPIO_ARMED_LED;
  capture_led->led_id = GPIO_CAPTURE_LED;
}

void splash(void)
{
	system("clear\n");
	// printf("-----------\n");
  printf("%.*s\n", 80, "================================================================================");
  char info[100];
	sprintf(info, "droneSAR MiloSAR Measurement Control %s\n\n", MILOSAR_VERSION);
  cprint(info, RESET, GREEN);
	system("unlink /etc/localtime\n"); 
	system("date");
  printf("%.*s\n", 80, "================================================================================");
}

