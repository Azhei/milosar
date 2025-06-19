#ifndef GPS_H
#define GPS_H

#include <gps.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "colour.h"
#include "constants.h"
#include "utils.h"

enum State {Idle = 0, Active = 1, Shutdown = 2};

typedef struct Gps_S 
{
    FILE* f;
    int is_locked;
    enum State state;
    int min_mode;
    int min_sats;    
    char* experiment_dir;
	pthread_t thread;
    struct gps_data_t data;
} Gps;

void *gps_worker(void *arg);
void print_gps(struct gps_data_t *gps_data);
void dinit_gps(Gps *gps);
void wait_for_fix(Gps *gps);
void save_data(Gps *gps);

#define HOST_IP     "localhost"
#define HOST_PORT   "2947"
#define TMP_FILE    "/tmp/gps.txt"

#endif