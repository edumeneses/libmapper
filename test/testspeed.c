#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

int automate = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig = 0;
mapper_signal recvsig = 0;

int numModes = 2;
int mode = 0;
int use_instance = 1;
int iterations = 100000;
int counter = 0;
int received = 0;
int done = 0;

double times[2];
float value;

void switch_modes();
void print_results();

/*! Internal function to get the current time. */
static double get_current_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0; 
}

/*! Creation of a local source. */
int setup_source()
{
    source = mdev_new("testSpeedSend", 0);
    if (!source)
        goto error;
    printf("source created.\n");

    sendsig = mdev_add_output(source, "/outsig", 1, 'f', 0, 0, 0);
    if (!sendsig)
        goto error;
    msig_reserve_instances(sendsig, 9);

    printf("Output signal registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));

    return 0;

  error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig, int instance_id, mapper_db_signal props,
                   mapper_timetag_t *timetag, void *v)
{
    counter = (counter+1)%10;
    if (v) {
       // printf("%i --> destination %s instance %ld got %f\n", received,
         //      props->name, (long)instance_id, (*(float*)v));
        if (++received >= iterations)
            switch_modes();
        if (use_instance) {
            msig_update_instance(sendsig, counter, &value);
        }
        else
            msig_update(sendsig, &value);
    }
    else
        printf("--> destination %s instance %ld got NULL\n",
               props->name, (long)instance_id);
}

void overflow_handler(mapper_signal sig, int group, int id)
{
    printf("OVERFLOW!!\n");
    msig_reserve_instances(sig, 1);
}

/*! Creation of a local destination. */
int setup_destination()
{
    destination = mdev_new("testSpeedRecv", 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    recvsig = mdev_add_input(destination, "/insig", 1, 'f',
                             0, 0, 0, insig_handler, 0);
    if (!recvsig)
        goto error;
    msig_reserve_instances(recvsig, 9);

    printf("Input signal registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(destination));

    return 0;

  error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}

void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 25);
        mdev_poll(destination, 25);
    }
}

void connect_signals()
{
    char source_name[1024], destination_name[1024];

    printf("%s\n", mdev_name(source));
    printf("%s\n", mdev_name(destination));

    lo_address a = lo_address_new_from_url("osc.udp://224.0.1.3:7570");
    lo_address_set_ttl(a, 1);

    lo_send(a, "/link", "ss", mdev_name(source), mdev_name(destination));

    // wait for link to be processed
    int j = 50;
    while (j >= 0) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        j--;
    }

    msig_full_name(sendsig, source_name, 1024);
    msig_full_name(recvsig, destination_name, 1024);

    lo_send(a, "/connect", "ssssss", source_name, destination_name, "@mode", "expression", "@expression", "y=sin(x)+1.9");

    lo_address_free(a);

    // wait for connection to be processed
    j = 50;
    while (j >= 0) {
        mdev_poll(source, 10);
        mdev_poll(destination, 10);
        j--;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

void switch_modes()
{
    // possible modes: bypass/expression/calibrate, clipmodes, instances, instance-stealing
    printf("SWITCHING MODES...\n");
    received = 0;
    switch (mode)
    {
        case 0:
            times[1] = get_current_time();
            times[0] = times[1] - times[0];
            use_instance = 0;
            mode++;
            break;
        default:
            times[1] = get_current_time() - times[1];
            done = 1;
            break;
    }
}

void print_results()
{
    int i;
    printf("\n*****************************************************\n");
    printf("\nRESULTS OF SPEED TEST:\n");
    for (i=0; i<numModes; i++)
        printf("mode %i: %i messages processed in %f seconds\n", i, iterations, times[i]);
    printf("\n*****************************************************\n");
}

int main()
{
    int result = 0;

    value = (float)rand();

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    if (automate)
        connect_signals();

    // start things off
    printf("STARTING TEST...\n");
    times[0] = get_current_time();
    msig_update_instance(sendsig, counter++, &value);
    while (!done) {
        mdev_poll(destination, 0);
        mdev_poll(source, 0);
    }
    goto done;

  done:
    cleanup_destination();
    cleanup_source();
    print_results();
    return result;
}
