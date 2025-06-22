#include "synth.h"
#include <time.h>


void parse_ramp_file(Synthesizer *synth)
{
	//ensure that the register array is cleared
	memset(synth->registers, 0, sizeof(synth->registers));
	
	//reset all ramp parameters
	for(int i = 0; i < MAX_RAMPS; i++)
	{
		synth->ramps[i].number = i;  
		synth->ramps[i].bandwidth = 0;  
		synth->ramps[i].next = 0;    
		synth->ramps[i].trigger = 0;
		synth->ramps[i].reset = 0;
		synth->ramps[i].flag = 0;	
		synth->ramps[i].doubler = 0;
		synth->ramps[i].length = 0;
		synth->ramps[i].increment = 0;
	}	
	
	//char* dir = "ramps/";
	char* dir = "";
	char* path = (char*)malloc(strlen(dir) + strlen(synth->parameter_file));
	strcpy(path, dir);
	strcat(path, synth->parameter_file);	
	
	if (ini_parse(path, handler, synth) < 0) 
	{
		printf("File not found: %s\n", path);
		ASSERT(FAIL, "Could not open Synth .ini file.\n");
		exit(EXIT_FAILURE);
	}   
}


//handler function called for every element in the ini file
//current implementation is inefficient, but no alternative could be found
int handler(void* pointer, const char* section, const char* attribute, const char* value)
{			
	char* rampSection = (char*)malloc(50*sizeof(char));
	Synthesizer* synth = (Synthesizer*)pointer;
	
	#define MATCH(s, n) strcmp(section, s) == 0 && strcmp(attribute, n) == 0
	
	if (MATCH("setup", "frac_num")) synth->fractional_numerator = atoi(value);
	
	for(int i = 0; i < MAX_RAMPS; i++)
	{
		sprintf(rampSection, "ramp%i", i);

		if (MATCH(rampSection, "length")) 			synth->ramps[i].length    = atof(value); 
		else if (MATCH(rampSection, "bandwidth")) 	synth->ramps[i].bandwidth = atof(value);  
		else if (MATCH(rampSection, "increment")) 	synth->ramps[i].increment = atof(value);
		else if (MATCH(rampSection, "next")) 		synth->ramps[i].next      = atoi(value);    
		else if (MATCH(rampSection, "trigger")) 	synth->ramps[i].trigger   = atoi(value);
		else if (MATCH(rampSection, "reset")) 		synth->ramps[i].reset     = atoi(value);
		else if (MATCH(rampSection, "flag")) 		synth->ramps[i].flag      = atoi(value);	
		else if (MATCH(rampSection, "doubler")) 	synth->ramps[i].doubler   = atoi(value);	
	}	
	
	//TODO: include error detection, currently always returns 1
	return 1;	
}


void calc_parameters(Synthesizer *synth, Configuration *config)
{	
	if (config->is_debug) 
	{
		cprint("\n[**] ", BRIGHT, CYAN);	
		printf("Synthesizer %i:\n", synth->id);
		printf("Parameter file: %s\n", synth->parameter_file);
		printf("Fractional numerator: %d (%f [Hz])\n", synth->fractional_numerator, get_vco_frequency(synth->fractional_numerator));		
		printf("| NUM | NXT | RST | DBL |   LEN |        INC |      BNW |\n");
	}
	
	for (int i = 0; i < MAX_RAMPS; i++)
	{
		//limit the maximum ramp length to prevent overflow
		if (synth->ramps[i].length > MAX_RAMP_LENGTH)
		{
			cprint("[!!] ", BRIGHT, RED);
			printf("Synth %i, ramp %i length set to maximum.\n", synth->id, i);
			synth->ramps[i].length = (uint16_t)MAX_RAMP_LENGTH;			
		}
		
		//calculate increment = (bandwidth [Hz] * divider * (2^24 - 1))/(phase detector frequency [MHz] * ramp_length)
		if ((synth->ramps[i].length != 0) && (synth->ramps[i].increment == 0))
		{
			synth->ramps[i].increment = (synth->ramps[i].bandwidth*RF_OUT_DIVIDER*FRAC_DENOMINATOR)/(PD_CLK*synth->ramps[i].length);			
		}
		
		//limit the maximum ramp increment to prevent overflow
		if (synth->ramps[i].increment > MAX_RAMP_INC)
		{
			cprint("[!!] ", BRIGHT, RED);
			printf("Synth %i, ramp %i increment set to maximum.\n", synth->id, i);
			synth->ramps[i].length = (uint16_t)MAX_RAMP_INC;			
		}		

		//assuming positive increment implies up-ramp
		if (synth->ramps[i].increment > 0.0)
		{
			synth->up_ramp_length = synth->ramps[i].doubler ? 2*synth->ramps[i].length : synth->ramps[i].length;
			synth->up_ramp_increment = synth->ramps[i].doubler ? (uint64_t)round(synth->ramps[i].increment/2) : (uint64_t)round(synth->ramps[i].increment);
		}
		
		//perform two's complement for negative values: 2^30 - value  
		if (synth->ramps[i].increment < 0.0)
		{
			synth->ramps[i].increment = pow(2, 30) + synth->ramps[i].increment;
		}		
		
		//set bit 31 if doubler key is true
		if (synth->ramps[i].doubler)
		{
			synth->ramps[i].increment = (uint64_t)round(synth->ramps[i].increment) | (uint64_t)pow(2, 31);	
		}
		else
		{
			synth->ramps[i].increment = (uint64_t)round(synth->ramps[i].increment);
		}
		
		//scheme to generate next-trigger-reset for R92, R96, R103 etc.. 
		synth->ramps[i].ntr = 0;
		
		synth->ramps[i].ntr += (synth->ramps[i].next << 5) & 0xFF;
		synth->ramps[i].ntr += (synth->ramps[i].trigger << 3) & 0xFF;
		synth->ramps[i].ntr += (synth->ramps[i].reset << 2) & 0xFF;
		synth->ramps[i].ntr += (synth->ramps[i].flag << 0) & 0xFF;
		
		if ((config->is_debug) && (synth->ramps[i].next + synth->ramps[i].length + synth->ramps[i].increment + synth->ramps[i].reset != 0))
		{
			printf("|   %i |   %i |   %i |   %i | %5i | %10.0f | %8.3f |\n", 
			synth->ramps[i].number, synth->ramps[i].next, synth->ramps[i].reset, synth->ramps[i].doubler, synth->ramps[i].length, 
			synth->ramps[i].increment, get_bandwidth(synth->ramps[i].increment, synth->ramps[i].length)/1e6);
		}		
	}	
	if (config->is_debug) 
	{
		printf("\n");
	}

	//calculate the equivalent binary values
	synth->bin_fractional_numerator = (int*)malloc(24*sizeof(int));
	memset(synth->bin_fractional_numerator, 0, 24*sizeof(int));
	decimal_to_binary(synth->fractional_numerator, synth->bin_fractional_numerator);	
	
	for (int i = 0; i < MAX_RAMPS; i++)
	{	
		synth->ramps[i].binIncrement     = (int*)malloc(32*sizeof(int));
		synth->ramps[i].binLength        = (int*)malloc(16*sizeof(int));	
		synth->ramps[i].binNextTrigReset = (int*)malloc(8*sizeof(int));		
		
		memset(synth->ramps[i].binIncrement, 	 0, 32*sizeof(int));
		memset(synth->ramps[i].binLength, 		 0, 16*sizeof(int));
		memset(synth->ramps[i].binNextTrigReset, 0,  8*sizeof(int));		
		
		decimal_to_binary(synth->ramps[i].increment, synth->ramps[i].binIncrement);
		decimal_to_binary(synth->ramps[i].length, synth->ramps[i].binLength);
		decimal_to_binary(synth->ramps[i].ntr, synth->ramps[i].binNextTrigReset);	
	}
}


void reset_synths(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth)
{
	set_register_parallel(gpio, tx_synth, lo_synth, 2, 0b00000100);
}


void set_ramping(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth, int is_ramping)
{
	if (is_ramping)
		set_register_parallel(gpio, tx_synth, lo_synth, 58, 0b00010001); //note: this value assumes RAMP_TRIG_A = TRIG1 terminal rising edge
	else
		set_register_parallel(gpio, tx_synth, lo_synth, 58, 0b00010000); //note: this value assumes RAMP_TRIG_A = TRIG1 terminal rising edge
}


void printBinary(int* binaryValue, int paddedSize)
{
	for (int i = paddedSize - 1; i >= 0; i--)
	{
		printf("%d", binaryValue[i]);
	}
}


void decimal_to_binary(uint64_t decimalValue, int* binaryValue)
{
	int i = 0;
	
	while(decimalValue > 0)
	{
		binaryValue[i] = decimalValue%2;
		decimalValue = decimalValue/2;
		i++;
	}
}


void load_registers(const char* filename, Synthesizer *synth)
{
	FILE *templateFile;
	char line[86][15];
	char trash[15];
	
	//Open specified file
	templateFile = fopen(filename,"r");
	
	//Check if file correctly opened
	if (templateFile == 0)
	{
		cprint("[!!] ", BRIGHT, RED);
		printf("Could not open %s. Check that the file name is correct.\n", filename);
		exit(0);
	}
	else
	{
		for (int l = 0; l < 86; l++)
		{
			fscanf(templateFile, "%s",trash);
			fscanf(templateFile, "%s",line[l]);
			
			//get hex values and convert to decimal
			char hexValue[] = {line[l][6], line[l][7]};			
			int decimalValue = strtoul(hexValue, NULL, 16);			
			
			//convert decimal to binary and store in register array
			decimal_to_binary(decimalValue, synth->registers[85 - l]);					
		}
	}

	fclose(templateFile);

	//for every ramp
	for (int i = 7; i >= 0; i--)
	{
		//for every bit
		for (int j = 7; j >= 0; j--)
		{
			synth->registers[92 + 7*i][j] = synth->ramps[i].binNextTrigReset[j];
		}

		//for every bit
		for (int j = 15; j >= 8; j--)
		{
			synth->registers[91 + 7*i][j - 8] = synth->ramps[i].binLength[j];
		}

		//for every bit
		for (int j = 7; j >= 0; j--)
		{
			synth->registers[90 + 7*i][j] = synth->ramps[i].binLength[j];
		}
		
		//for every bit
		for (int j = 31; j >= 24; j--)
		{
			synth->registers[89 + 7*i][j - 24] = synth->ramps[i].binIncrement[j];
		}
		
		//for every bit
		for (int j = 23; j >= 16; j--)
		{
			synth->registers[88 + 7*i][j - 16] = synth->ramps[i].binIncrement[j];
		}		
		
		//for every bit
		for (int j = 15; j >= 8; j--)
		{
			synth->registers[87 + 7*i][j - 8] = synth->ramps[i].binIncrement[j];
		}
		
		//for every bit
		for (int j = 7; j >= 0; j--)
		{
			synth->registers[86 + 7*i][j] = synth->ramps[i].binIncrement[j];
		}
		
		
	}
	
	//for every bit
	for (int j = 23; j >= 16; j--)
	{
		synth->registers[21][j - 16] = synth->bin_fractional_numerator[j];
	}
	
	//for every bit
	for (int j = 15; j >= 8; j--)
	{
		synth->registers[20][j - 8] = synth->bin_fractional_numerator[j];
	}
	
	//for every bit
	for (int j = 7; j >= 0; j--)
	{
		synth->registers[19][j] = synth->bin_fractional_numerator[j];
	}
}


void init_pins(Synthesizer *synth)
{
	synth->latch = (uint64_t)(1 << (0 + 4*synth->id));
	synth->data  = (uint64_t)(1 << (1 + 4*synth->id));
	synth->clock = (uint64_t)(1 << (2 + 4*synth->id));
	synth->trig  = (uint64_t)(1 << (3 + 4*synth->id));
}


void set_register(void* gpio, Synthesizer *synth, int address, int value)
{
	int binAddress[16];
	int binValue[8];
	
	memset(binAddress, 0, 16*sizeof(int));
	memset(binValue, 0, 8*sizeof(int));
	
	decimal_to_binary(address, binAddress);	
	decimal_to_binary(value, binValue);
 
	set_pin(gpio, synth->latch, HIGH); 			//set latch high
	usleep(1);
	set_pin(gpio, synth->clock, HIGH);			//set clock high
	usleep(1);
	set_pin(gpio, synth->latch, LOW); 			//set latch low
	usleep(1);
	set_pin(gpio, synth->data, LOW); 			//set data low
	usleep(1);
	set_pin(gpio, synth->clock, LOW);			//set clock low
	usleep(1);

	for (int j = 15; j >= 0 ; j--)
	{
		if (binAddress[j] == 1)
			set_pin(gpio, synth->data, HIGH); 	//set data high
		else
			set_pin(gpio, synth->data, LOW); 	//set data low
			
		usleep(1);	
		set_pin(gpio, synth->clock, HIGH);		//set clock high
		usleep(1);
		set_pin(gpio, synth->clock, LOW);		//set clock low		
		usleep(1);
	}			
	
	//Write register data
	for(int j = 7; j >= 0; j--)
	{
		if (binValue[j] == 1)
			set_pin(gpio, synth->data, HIGH); 	//set data high
		else
			set_pin(gpio, synth->data, LOW); 	//set data low
		
		usleep(1);	
		set_pin(gpio, synth->clock, HIGH);		//set clock high
		usleep(1);
		set_pin(gpio, synth->clock, LOW);		//set clock low		
		usleep(1);
	}
	
	set_pin(gpio, synth->latch, HIGH); 			//set latch high
	usleep(1);
	set_pin(gpio, synth->data, LOW); 			//set data low
}


void set_register_parallel(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth, int address, int value)
{
	int binAddress[16];
	int binValue[8];
	
	memset(binAddress, 0, 16*sizeof(int));
	memset(binValue, 0, 8*sizeof(int));
	
	decimal_to_binary(address, binAddress);	
	decimal_to_binary(value, binValue);
 
	set_pin(gpio, (tx_synth->latch | lo_synth->latch), HIGH); 			//set latch high
	usleep(1);
	set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);			//set clock high
	usleep(1);
	set_pin(gpio, (tx_synth->latch | lo_synth->latch), LOW); 			//set latch low
	usleep(1);
	set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 				//set data low
	usleep(1);
	set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);			//set clock low
	usleep(1);

	for (int j = 15; j >= 0 ; j--)
	{
		if (binAddress[j] == 1)
			set_pin(gpio, (tx_synth->data | lo_synth->data), HIGH); 	//set data high
		else
			set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 		//set data low
			
		usleep(1);	
		set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);		//set clock high
		usleep(1);
		set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);		//set clock low		
		usleep(1);
	}			
	
	//Write register data
	for(int j = 7; j >= 0; j--)
	{
		if (binValue[j] == 1)
			set_pin(gpio, (tx_synth->data | lo_synth->data), HIGH); 	//set data high
		else
			set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 		//set data low
		
		usleep(1);	
		set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);		//set clock high
		usleep(1);
		set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);		//set clock low		
		usleep(1);
	}
	
	set_pin(gpio, (tx_synth->latch | lo_synth->latch), HIGH); 			//set latch high
	usleep(1);
	set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 				//set data low	
}

void flash_synth(void* gpio, Synthesizer *synth)
{
	int start_address[16];
	memset(start_address, 0, 16*sizeof(int));
	decimal_to_binary((NUM_REGISTERS - 1), start_address);
	
	int address_flag = false;
	
	for (int i = (NUM_REGISTERS - 1); i >= 0; i--)
	{
		if (address_flag == false)
		{
			set_pin(gpio, synth->latch, HIGH); 			//set latch high
			usleep(1);
			set_pin(gpio, synth->clock, HIGH);			//set clock high
			usleep(1);
			set_pin(gpio, synth->latch, LOW); 			//set latch low
			usleep(1);
			set_pin(gpio, synth->data, LOW); 			//set data low
			usleep(1);
			set_pin(gpio, synth->clock, LOW);			//set clock low
			usleep(1);

			for (int j = 15; j >= 0 ; j--)
			{
				if (start_address[j] == 1)
					set_pin(gpio, synth->data, HIGH); 	//set data high
				else
					set_pin(gpio, synth->data, LOW); 	//set data low
					
				usleep(1);	
				set_pin(gpio, synth->clock, HIGH);		//set clock high
				usleep(1);
				set_pin(gpio, synth->clock, LOW);		//set clock low		
				usleep(1);
			}			
			
			//Only do this the first loop iteration, set address_flag
			address_flag = true;
		}

		//Write register data
		for(int j = 7; j >= 0; j--)
		{
			if (synth->registers[i][j] == 1)
				set_pin(gpio, synth->data, HIGH); 		//set data high
			else
				set_pin(gpio, synth->data, LOW); 		//set data low
			
			usleep(1);	
			set_pin(gpio, synth->clock, HIGH);			//set clock high
			usleep(1);
			set_pin(gpio, synth->clock, LOW);			//set clock low		
			usleep(1);
		}
	}
	
	set_pin(gpio, synth->latch, HIGH); 					//set latch high
	usleep(1);
	set_pin(gpio, synth->data, LOW); 					//set data low
}
 
 
void flash_synths(void* gpio, Synthesizer *tx_synth, Synthesizer *lo_synth)
{
	int start_address[16];
	memset(start_address, 0, 16*sizeof(int));
	decimal_to_binary((NUM_REGISTERS - 1), start_address);
	
	int address_flag = false;
	
	for (int i = (NUM_REGISTERS - 1); i >= 0; i--)
	{
		if (address_flag == false)
		{
			set_pin(gpio, (tx_synth->latch | lo_synth->latch), HIGH); 			//set latch high
			usleep(1);
			set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);			//set clock high
			usleep(1);
			set_pin(gpio, (tx_synth->latch | lo_synth->latch), LOW); 			//set latch low
			usleep(1);
			set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 				//set data low
			usleep(1);
			set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);			//set clock low
			usleep(1);

			for (int j = 15; j >= 0 ; j--)
			{
				if (start_address[j] == 1)
					set_pin(gpio, (tx_synth->data | lo_synth->data), HIGH); 	//set data high
				else
					set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 		//set data low
					
				usleep(1);	
				set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);		//set clock high
				usleep(1);
				set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);		//set clock low		
				usleep(1);
			}			
			
			//Only do this the first loop iteration, set address_flag
			address_flag = true;
		}

		//Write register data
		for(int j = 7; j >= 0; j--)		
		{
			if ((tx_synth->registers[i][j] == 1) && (lo_synth->registers[i][j] == 1))
			{
				set_pin(gpio, (tx_synth->data | lo_synth->data), HIGH); 		//set data high
			}
			else if ((tx_synth->registers[i][j] == 0) && (lo_synth->registers[i][j] == 0))
			{	
				set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 			//set data low
			}
			else if ((tx_synth->registers[i][j] == 1) && (lo_synth->registers[i][j] == 0))
			{	
				set_pin(gpio, (tx_synth->data), HIGH); 			//set data low
				set_pin(gpio, (lo_synth->data), LOW); 			//set data low
			}	
			else if ((tx_synth->registers[i][j] == 0) && (lo_synth->registers[i][j] == 1))
			{	
				set_pin(gpio, (tx_synth->data), LOW); 			//set data low
				set_pin(gpio, (lo_synth->data), HIGH); 			//set data low
			}		
			
			usleep(1);	
			set_pin(gpio, (tx_synth->clock | lo_synth->clock), HIGH);			//set clock high
			usleep(1);
			set_pin(gpio, (tx_synth->clock | lo_synth->clock), LOW);			//set clock low		
			usleep(1);
		}
	}
	
	set_pin(gpio, (tx_synth->latch | lo_synth->latch), HIGH); 			//set latch high
	usleep(1);
	set_pin(gpio, (tx_synth->data | lo_synth->data), LOW); 				//set data low
} 
 
 
time_t start_experiment(void* gpio, void* tcu, Configuration *config)
{
	//clock cycles per PRI
	float cycles_per_pri = ADC_RATE/config->prf;

	if (fmod(ADC_RATE, config->prf) != 0.0f)
	{
		printf("PRF selection not appropriate! PRF must be an integer divisor of the clock rate.\n");
		exit(0);
	}

	if (cycles_per_pri > pow(2, 24) - 1)
	{
		printf("PRF too low!\n");
		exit(0);
	}
	
	//set the TCU parameters in the GPIO register
	uint32_t gpio_register = 0;		
	gpio_register |= ((uint32_t)cycles_per_pri << 8);
	set_reg(gpio, gpio_register);

	if (config->n_pulses > pow(2, 30) - 1)
	{
		printf("Number of pulses is limited to a 30-bit value.\n");
		exit(0);
	}
	
	//set all values without enable %TODO: move earlier to reduce time offset
	uint64_t tcu_register = 0;
	tcu_register |= (0 << 0); //clear enable flag
	tcu_register |= (0 << 1); //clear is_sync flag
	tcu_register |= ((uint64_t)config->n_pulses << 2); //set n_pulses to generate
	tcu_register |= ((uint64_t)config->switch_mode << 32); //set pulse_width
	set_reg(tcu, tcu_register);

	tcu_register |= (1 << 0); //set enable flag
	set_reg(tcu, tcu_register);
	time_t timer;
	time_t tcu_trigger_time = time(&timer);	// time the TCU is triggered

	cprint("\n[OK] ", BRIGHT, GREEN);
	printf("Experiment Progress:\n\n");

	return tcu_trigger_time;
}



void config_experiment(Configuration *config, Synthesizer *tx_synth, Synthesizer *lo_synth)
{
	//calculate the number of PRF pulses to generate 
	config->n_pulses = config->n_seconds*config->prf;

	//number of stored samples per pri over total samples per pri
	float up_down_ratio = (float)(config->end_index - config->start_index + 1)/config->n_samples_per_pri;

	float data_size_bytes = N_CHANNELS*BYTES_PER_WRITE*(ADC_RATE/config->decimation_factor)*config->n_seconds*up_down_ratio/config->presum_factor;

	config->n_buffers = (int)ceil(data_size_bytes/S2MB);

	config->switch_factor = config->switch_mode == 3 ? 2 : 1;

	cprint("[OK] ", BRIGHT, GREEN);
	printf("Measurement Parameters:\n");

	printf("Switch Mode:\t\t%i\t[0=OFF, 1=RF_1, 2=RF_2, 3=INTERLEAVE]\n", config->switch_mode);
	printf("Length:\t\t\t%i\t[s]\n", config->n_seconds);
	printf("Pulses:\t\t\t%i\n", config->n_pulses);
	printf("TX PRF:\t\t\t%i\t[Hz]\n", config->prf);		
	printf("Presum Factor:\t\t%i\t[pulses]\n", config->presum_factor);
	printf("Switch Factor:\t\t%i\t[pulses]\n", config->switch_factor);
	printf("RX PRF:\t\t\t%i\t[Hz]\n", config->prf/config->presum_factor/config->switch_factor);
	printf("\n");

	printf("Decimation Factor:\t%i\n", config->decimation_factor);
	printf("Chop Factor:\t\t%.3f\n", up_down_ratio);
	printf("Data Size:\t\t%0.3f\t[MB]\n", (data_size_bytes/S1MB));
	printf("Size on Disk:\t\t%i\t[MB]\n", 2*config->n_buffers);
	printf("Data Rate:\t\t%.3f\t[MB/s]\n", data_size_bytes/(config->n_seconds * S1MB));
	
	printf("\n");	
	
	//allocate memory 
	config->time_stamp = (char*)malloc(20*sizeof(char));
	config->experiment_dir = (char*)malloc(100*sizeof(char));
	config->path_summary = (char*)malloc(100*sizeof(char));
	
	//get the experiment time stamp
	time_t timer = time(&timer);
  	struct tm* tm_info  = localtime(&timer);
	strftime(config->time_stamp, 20, "%y_%m_%d_%H_%M_%S", tm_info);
	
	//set the experiment directory
	sprintf(config->experiment_dir, "%s/%s/", config->storage_dir, config->time_stamp);
	
	//set the summary file path
	sprintf(config->path_summary, "%s%s", config->experiment_dir, "summary.ini");
	
	//make the experiment directory
	char command[100];
	sprintf(command, "mkdir %s/%s", config->storage_dir, config->time_stamp);		
	system(command);
	
	FILE* f;
	f = fopen(config->path_summary, "w");
	
	//Check if file correctly opened
	if (f == 0)
	{
		cprint("[!!] ", BRIGHT, RED);
		printf("Could not open summary file. Ensure you have read-write access\n");
	}
	else
	{
		//copy setup ini file
		//sprintf(command, "cp setup.ini %s", config->experiment_dir);
		sprintf(command, "cp /opt/redpitaya/milosar/setup.ini %s", config->experiment_dir);
		system(command);

		//copy register_template file
		//sprintf(command, "cp template/register_template.txt %s", config->experiment_dir);
		sprintf(command, "cp /opt/redpitaya/milosar/template/register_template.txt %s", config->experiment_dir);
		system(command);
		
		//copy ramp ini parameter files
		//sprintf(command, "cp ramps/%s %s", tx_synth->parameter_file, config->experiment_dir);
		sprintf(command, "cp %s %s", tx_synth->parameter_file, config->experiment_dir);
		system(command);
		
		if (tx_synth->parameter_file != lo_synth->parameter_file)
		{
			//sprintf(command, "cp ramps/%s %s", lo_synth->parameter_file, config->experiment_dir); 
			sprintf(command, "cp %s %s", lo_synth->parameter_file, config->experiment_dir);
			system(command);
		}
		
		//print summary file 
		fprintf(f, "[general]\r\n");		

		//ask for operator comment
		cprint("[??] ", BRIGHT, BLUE);
		printf("Operator Comment [512]: ");
		
		char comment[512] = "\0";
		// scanf("%[^\n]s", comment);
		if (comment[0] == '\0') strcpy(comment, "none");
    printf("\n");

		//get time just before experiment start
		char time_now[20];
		timer = time(&timer);
		tm_info  = localtime(&timer);
		strftime(time_now, 20, "%y_%m_%d_%H_%M_%S", tm_info);

		fprintf(f, "time_stamp        = %s\r\n", time_now);
		fprintf(f, "operator_comment  = %s\r\n", comment);
		fprintf(f, "capture_delay     = %u\r\n", config->capture_delay);

		fprintf(f, "\n[dataset]\r\n");
		fprintf(f, "switch_mode       = %i\r\n", config->switch_mode);
		fprintf(f, "bytes             = %d\r\n", config->n_buffers*S2MB);
		fprintf(f, "n_buffers         = %d\r\n", config->n_buffers);
		fprintf(f, "n_seconds         = %i\r\n", config->n_seconds);
		fprintf(f, "prf               = %i\r\n", config->prf);
		fprintf(f, "decimation_factor = %d\r\n", config->decimation_factor);
		fprintf(f, "sampling_rate     = %.2f\r\n", ADC_RATE/config->decimation_factor);
		fprintf(f, "n_counter         = %i\r\n", N_COUNTER);
		fprintf(f, "channel_a_phase_increment = %i\r\n", config->channel_a_phase_increment);
		fprintf(f, "channel_b_phase_increment = %i\r\n", config->channel_b_phase_increment);

		fprintf(f, "\n[integration]\r\n");
		fprintf(f, "n_samples_per_pri = %d\r\n", config->n_samples_per_pri);
		fprintf(f, "n_pris            = %d\r\n", config->presum_factor);
		fprintf(f, "start_index       = %d\r\n", config->start_index);
		fprintf(f, "end_index         = %d\r\n", config->end_index);
	
		fprintf(f, "\n[tx_synth]\r\n");
		fprintf(f, "id                    = %d\r\n", tx_synth->id);
		fprintf(f, "frequency_offset      = %.5f\r\n", get_vco_frequency(tx_synth->fractional_numerator));
		fprintf(f, "fractional_numerator  = %d\r\n", tx_synth->fractional_numerator);	
		fprintf(f, "parameter_file        = %s\r\n", tx_synth->parameter_file);	
		fprintf(f, "up_ramp_increment     = %d\r\n", tx_synth->up_ramp_increment);
		fprintf(f, "up_ramp_length        = %d\r\n", tx_synth->up_ramp_length);
		
		fprintf(f, "\n[dx_synth]\r\n");
		fprintf(f, "id                    = %d\r\n", lo_synth->id);
		fprintf(f, "frequency_offset      = %.5f\r\n", get_vco_frequency(lo_synth->fractional_numerator));
		fprintf(f, "fractional_numerator  = %d\r\n", lo_synth->fractional_numerator);	
		fprintf(f, "parameter_file        = %s\r\n", lo_synth->parameter_file);	
		fprintf(f, "up_ramp_increment     = %d\r\n", lo_synth->up_ramp_increment);
		fprintf(f, "up_ramp_length        = %d\r\n", lo_synth->up_ramp_length);

		fclose(f);
	}
}

double get_vco_frequency(uint32_t fractional_numerator)
{
	return PD_CLK*(N_COUNTER + fractional_numerator/FRAC_DENOMINATOR)/RF_OUT_DIVIDER - RF_OUT_INIT_FREQ;
}


double get_bandwidth(uint64_t rampInc, uint16_t rampLen)
{
	if (rampInc & (uint64_t)pow(2, 31))
	{
		rampInc &= ~(1 << 31);
	}
	
	if (rampInc < (pow(2, 24) - 1))
		return (rampInc*rampLen*PD_CLK)/pow(2, 26); //TODO replace with constants
	else
		return ((rampInc - pow(2, 30))*rampLen*PD_CLK)/pow(2, 26);
}


float get_duration(int n_buffers, int prf, int int_factor, int e_sample, int s_sample, int bytes_per_sample)
{
	return n_buffers * (S2MB/(((float)prf/int_factor) * (e_sample - s_sample) * bytes_per_sample));
}


int get_n_chunks(int n_buffers, int e_sample, int s_sample, int bytes_per_sample)
{
	return (n_buffers*S2MB)/(bytes_per_sample*(e_sample - s_sample));
}


