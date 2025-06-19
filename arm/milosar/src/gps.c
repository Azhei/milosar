#include "gps.h"
#include "constants.h"

void *gps_worker(void *arg)
{
    Gps *gps = (Gps *)arg;
    gps->f = fopen(TMP_FILE, "w");
    int rc;

    if ((rc = gps_open(HOST_IP, HOST_PORT, &gps->data)) == -1) 
    {
        printf("Failed to open gpsd daemon socket at %s:%s.\nCode: %d.\nReason: %s.\n",HOST_IP, HOST_PORT, rc, gps_errstr(rc));
        exit(EXIT_FAILURE);
    } else
    {
        cprint("[**] ", BRIGHT, CYAN);
        printf("Connected to gpsd on %s:%s\n", HOST_IP, HOST_PORT);
    }

    // set watch policy 
    gps_stream(&gps->data, WATCH_ENABLE | WATCH_JSON, NULL);

    while (gps->state != Shutdown)
    {
        // blocking check with timeout to see if input is waiting.
        if (gps_waiting(&gps->data, 2e6)) 
        {
            // blocking read for data from the daemon
            if ((rc = gps_read(&gps->data)) == -1) 
            {
                printf("Error occurred reading GPS data.\nCode: %d.\nReason: %s\n", rc, gps_errstr(rc));
                gps_stream(&gps->data, WATCH_DISABLE, NULL);
                gps_close (&gps->data);
	            exit(EXIT_FAILURE);
            } 
            else 
            {      
                // interpret data from the GPS receiver.
                if (gps->data.set & PACKET_SET) // at least one complete JSON response has arrived since the last read
                {
                    if ((gps->data.fix.mode >= gps->min_mode) && (gps->data.satellites_used >= gps->min_sats))
                    {                   
                        gps->is_locked = true;
                    } 
                    else
                    {
                        gps->is_locked = false;
                    }      

                    if ((gps->state == Active) && (gps->is_locked))
                    {
                        save_data(gps);
                        gps_clear_dop(&gps->data.dop);
                        gps_clear_fix(&gps->data.fix);
                    }                                 
                }
                else
                {
                    printf("No complete JSON response has arrived since the last read.\n");
                }
            }
        }
        else
        {       
	        // re-enable GPS stream if/when automatic disable occurs
            gps_stream(&gps->data, WATCH_ENABLE | WATCH_JSON, NULL);
        }
    }

    // all done
    fclose(gps->f);

    char command[100];
    sprintf(command, "cp %s %s\n", TMP_FILE, gps->experiment_dir);
    system(command);

    gps_stream(&gps->data, WATCH_DISABLE, NULL);
    gps_close(&gps->data);
    return NULL;
}


void print_gps(struct gps_data_t *gps_data)
{
    if (gps_data->set & PACKET_SET) // at least one complete JSON response has arrived since the last read
    {
        char iso_time[26];
        char* mode;
        
        printf("Status:\t\t%s\n", gps_data->status ? "FIX" : "NO FIX");

        switch (gps_data->fix.mode)
        {
            case 0: mode = "UNKNOWN"; break;
            case 1: mode = "NO FIX"; break;
            case 2: mode = "2D"; break;
            case 3: mode = "3D"; break;
        }

        printf("Mode:\t\t%s\n", mode);
        printf("Satellites:\t%i/%i\n", gps_data->satellites_used, gps_data->satellites_visible);
        printf("Latitude:\t%f\n", gps_data->fix.latitude);
        printf("Longitude:\t%f\n", gps_data->fix.longitude);
        printf("Speed:\t\t%f\n", gps_data->fix.speed);
        
        unix_to_iso8601(gps_data->fix.time, iso_time, 26);
        printf("GPS Time:\t%s\n", iso_time);

        unix_to_iso8601(time(NULL), iso_time, 26);
        printf("System Time:\t%s\n", iso_time);

        gps_clear_fix(&gps_data->fix);
    }
}


void dinit_gps(Gps *gps)
{
    gps_stream(&gps->data, WATCH_DISABLE, NULL);
    gps_close(&gps->data);
    free(gps);
    fclose(gps->f);
}


void wait_for_fix(Gps *gps)
{
    int displayed_count = 0;
    printf("\n\n\n\n\n\n\n\n\n");
    while(displayed_count < 5)
    {
        printf("\033[10A\n"); //move cursor up 
        printf("\033[J"); //clear screen from cursor down

        cprint("[??] ", BRIGHT, BLUE);
        printf("Waiting for GPS to acquire stable fix ");
        spin_cursor();
        printf("\n");	

        print_gps(&gps->data);			
        sleep(1);

        //display some fix data before moving on
        if (gps->is_locked)
        {
            displayed_count += 1;
        }
    }

    printf("\033[10A\n"); //move cursor up 
    printf("\033[J"); //clear screen from cursor down
}


void save_data(Gps *gps)
{
    fprintf(gps->f, "%lf, %f, %f, %f, %f, %f, %f, %f, %f, %f, %i, %i\n",    
            gps->data.fix.time, 
            gps->data.fix.ept,  
            gps->data.fix.latitude,
            gps->data.fix.epy, 
            gps->data.fix.longitude, 
            gps->data.fix.epx, 
            gps->data.fix.altitude, 
            gps->data.fix.epv, 
            gps->data.fix.speed, 
            gps->data.fix.eps, 
            gps->data.satellites_used, 
            gps->data.satellites_visible);
}