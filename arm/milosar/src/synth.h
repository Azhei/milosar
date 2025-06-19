#ifndef SYNTH_H
#define SYNTH_H

#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#include "constants.h"
#include "ini.h"
#include "utils.h"
#include "colour.h"
#include "reg.h"
#include "gps.h"
#include "led.h"

#define MAX_RAMPS 				8
#define NUM_REGISTERS 			142
#define FRAC_DENOMINATOR 		(pow(2, 24) - 1)
#define MAX_RAMP_LENGTH			(pow(2, 16) - 1)
#define MAX_RAMP_INC			(pow(2, 30) - 1)
#define N_COUNTER				75
#define RF_OUT_DIVIDER			4
#define RF_OUT_INIT_FREQ		(PD_CLK*N_COUNTER/RF_OUT_DIVIDER)

typedef struct 
{
	uint8_t number;
	uint8_t reset;
	uint8_t next;
	uint8_t trigger;
	uint8_t flag;
	uint8_t doubler;
	
	// next-trigger-reset
	int ntr;
	
	double bandwidth;	
	double increment;
	uint16_t length;		

	char* hexIncrement;
	char* hexLength;
	char* hexNextTrigReset;
	
	int* binIncrement;
	int* binLength;
	int* binNextTrigReset;	
} Ramp;

typedef struct
{
	int id;
	int *bin_fractional_numerator;
	int registers[NUM_REGISTERS][MAX_RAMPS];
	char *parameter_file;
	Ramp ramps[MAX_RAMPS];
	uint32_t fractional_numerator;
	uint64_t latch, data, clock, trig;
	int up_ramp_increment;
	int up_ramp_length;
} Synthesizer;

int handler(void* user, const char* section, const char* name, const char* value);

void parse_ramp_file(Synthesizer *synth);
void calc_parameters(Synthesizer *synth, Configuration *config);
void load_registers(const char* filename, Synthesizer *synth);
void reset_synths(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth);
void set_ramping(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth, int is_ramping);
void flash_synth(void* gpio, Synthesizer *synth);
void flash_synths(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth);
void init_pins(Synthesizer *synth);
void set_register(void* gpio, Synthesizer *synth, int address, int value);
void set_register_parallel(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth, int address, int value);
//void start_experiment(void* gpio, void* tcu, Configuration *config);
// time_t start_experiment(void* gpio, void* tcu, Configuration *config);
time_t start_experiment(void* gpio, void* tcu, Configuration *config);
void config_experiment(Configuration *config, Synthesizer *tx_synth, Synthesizer *lo_synth, Gps *gps);
void decimal_to_binary(uint64_t decimalValue, int* binaryValue);
void print_binary(int* binaryValue, int paddedSize);

double get_vco_frequency(uint32_t fractional_numerator);
double get_bandwidth(uint64_t ramp_increment, uint16_t ramp_length);

float get_duration(int n_buffers, int prf, int int_factor, int e_sample, int s_sample, int bytes_per_sample);
int get_n_chunks(int n_buffers, int e_sample, int s_sample, int bytes_per_sample);

#endif
