#ifndef CONSTANTS
#define CONSTANTS
#include <pthread.h>
#include <math.h>
#include <stdint.h>

//Set Operating Constants
#define ADC_RATE            125e6
#define DAC_RATE            125e6
#define PD_CLK              125e6
#define DAC_BUFFER_CAPACITY 4096
#define DAC_BIT_LENGTH      14
#define DDS_PHASE_WIDTH     32
#define BYTES_PER_WRITE     4
#define FIFO_DEPTH          8192
#define N_CHANNELS          2

//Set Common Data Sizes
#define S1MB (1 << 20) //2MB
#define S2MB (2 << 20) //2MB
#define S4MB (4 << 20) //4MB
#define SREG (4 << 10) //4KB

//Set Commonly Used Registers
#define STS_A_BASE_ADDR     0x40000000
#define TCU_BASE_ADDR       0x40001000
#define GPIO_BASE_ADDR      0x40002000
#define MAIN_LO_BASE_ADDR   0x40003000
#define REF_LO_BASE_ADDR    0x40004000

#define INT_BASE_ADDR       0x40006000
#define INDX_BASE_ADDR      0x40007000

#define GPIO_TRIGGER_LED    0x40009000
#define GPIO_POWER_LED      0   // RED
#define GPIO_ARMED_LED      1   // YELLOW
#define GPIO_CAPTURE_LED    2   // GREEN

#define DMA_A_BASE_ADDR     0x1E000000
#define DMA_B_BASE_ADDR     0x1E400000

#define SD_STORAGE_DIR      "/media/storage"
#define SYNTH_REG_TEMP_DIR  "/opt/redpitaya/milosar/template/register_template.txt"
#define SETUP_FILE          "/opt/redpitaya/milosar/setup.ini"
#define LOG_FILE            "log.txt"

#define OK    0
#define FAIL  -1

#define HIGH  1
#define LOW   0

#define true  1
#define false 0

#define SWITCH_INTERLEAVE 0
#define SWITCH_RF_1       1
#define SWITCH_RF_2       2

typedef struct Channel_S 
{
	void *dma;
	void *sts;
	uint32_t dma_base;
	uint32_t sts_base;
	char letter[1];	
	pthread_t thread;
} Channel;

typedef struct
{
	int is_debug;                   //is debug mode enabled
	int is_data_transfer;           //is data transfer to host enabled
	int is_gpsd;

	int channel_a_phase_increment;
	int channel_b_phase_increment;

	int n_buffers;                  //number of S2MB buffers to be recorded
	int n_seconds;                  //number of seconds to generate PRF pulse train
	int n_pulses;                   //number of PRF pulses to generate
	int prf;                        //prf square wave produced by fpga
	int decimation_factor;
	int switch_mode;
	int switch_factor;              // prf reduction resulting from interleaving

	char* time_stamp;               //experiment timestamp
	char* storage_dir;              //path to storage directory
	char* experiment_dir;           //path to storage directory	
	char* path_summary;             //filename of summary file including path

	//variables for fpga integrator
	int n_samples_per_pri;				
	int presum_factor;              //number of pris per integration  
	int start_index;
	int end_index;

	//variables for post-experiment data transfer
	char* host_name;
	char* host_ip;
	char* host_dir;

	char* bitstream;                //fpga bitsteam file name

	//variables for delaying the start of capture
	//	this is mainly used when a wired ethernet connection is used to initiate the capture
	//	but disconnected during actual capture sequence
	unsigned int capture_delay;     //number of seconds to delay capture, from start of capture sequence
  int is_status_leds;             // is the status LEDs enabled

} Configuration;

#endif
