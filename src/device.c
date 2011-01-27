
#include <lo/lo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include "config.h"
#include <mapper/mapper.h>

/*! Internal function to get the current time. */
static double get_current_time()
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
#else
#error No timing method known on this platform.
#endif
}

//! Allocate and initialize a mapper device.
mapper_device mdev_new(const char *name_prefix, int initial_port,
                       mapper_admin admin)
{
    if (initial_port == 0)
        initial_port = 9000;

    mapper_device md =
        (mapper_device) calloc(1, sizeof(struct _mapper_device));
    md->name_prefix = strdup(name_prefix);

    if (admin) {
        md->admin = admin;
        md->own_admin = 0;
    }
    else {
        md->admin = mapper_admin_new(0, 0, 0);
        md->own_admin = 1;
    }

    if (!md->admin) {
        mdev_free(md);
        return NULL;
    }

    mapper_admin_add_device(md->admin, md, name_prefix, initial_port);

    md->admin->port.on_lock = mdev_on_port_and_ordinal;
    md->admin->ordinal.on_lock = mdev_on_port_and_ordinal;
    md->routers = 0;
    md->extra = table_new();
    return md;
}

//! Free resources used by a mapper device.
void mdev_free(mapper_device md)
{
    int i;
    if (md) {
        if (md->admin && md->own_admin)
            mapper_admin_free(md->admin);
        for (i = 0; i < md->n_inputs; i++)
            msig_free(md->inputs[i]);
        if (md->inputs)
            free(md->inputs);
        for (i = 0; i < md->n_outputs; i++)
            msig_free(md->outputs[i]);
        if (md->outputs)
            free(md->outputs);
        if (md->extra)
            table_free(md->extra, 1);
        free(md);
    }
}

#ifdef __GNUC__
// when gcc inlines this with O2 or O3, it causes a crash. bug?
__attribute__ ((noinline))
#endif
static void grow_ptr_array(void **array, int length, int *size)
{
    if (*size < length && !*size)
        (*size)++;
    while (*size < length)
        (*size) *= 2;
    *array = realloc(*array, sizeof(void *) * (*size));
}

static void mdev_increment_version(mapper_device md)
{
    md->version ++;
    if (md->admin->registered) {
        md->update = 1;
    }
}

static mapper_signal_value_t *sv = 0;
static int handler_signal(const char *path, const char *types,
                          lo_arg **argv, int argc, lo_message msg,
                          void *user_data)
{
    mapper_signal sig = (mapper_signal) user_data;
    mapper_device md = sig->device;

    if (!md) {
        trace("error, sig->device==0\n");
        return 0;
    }
    sig->has_value = 1;
    int i;
    sv = (mapper_signal_value_t*) realloc(
                                          sv, sizeof(mapper_signal_value_t) * sig->props.length);
    switch (sig->props.type) {
        case 'f':
            for (i = 0; i < sig->props.length; i++) {
                sv[i].f = argv[i]->f;
                sig->value[i] = sv[i];
            }
            break;
        case 'd':
            for (i = 0; i < sig->props.length; i++) {
                sv[i].d = argv[i]->d;
                sig->value[i] = sv[i];
            }
            break;
        case 'i':
            for (i = 0; i < sig->props.length; i++) {
                sv[i].i32 = argv[i]->i;
                sig->value[i] = sv[i];
            }
            break;
        default:
            assert(0);
    }
    if (sig->handler) {
        sig->handler(sig, sv);
    }

    return 0;
}

// Add an input signal to a mapper device.
mapper_signal mdev_add_input(mapper_device md, const char *name, int length,
                             char type, const char *unit,
                             void *minimum, void *maximum,
                             mapper_signal_handler *handler,
                             void *user_data)
{
    if (mdev_get_input_by_name(md, name, 0))
        return 0;
    mapper_signal sig = msig_new(name, length, type, 0, unit, minimum, 
                                 maximum, handler, user_data);
    md->n_inputs++;
    grow_ptr_array((void **) &md->inputs, md->n_inputs,
                   &md->n_alloc_inputs);

    mdev_increment_version(md);

    md->inputs[md->n_inputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;

    if (!md->server)
        mdev_start_server(md);
    else {
        char *type_string = (char*) malloc(sig->props.length + 1);
        memset(type_string, sig->props.type, sig->props.length);
        type_string[sig->props.length] = 0;
        lo_server_add_method(md->server,
                             sig->props.name,
                             type_string,
                             handler_signal, (void *) (sig));
        free(type_string);
    }

    return sig;
}

// Add an output signal to a mapper device.
mapper_signal mdev_add_output(mapper_device md, const char *name, int length,
                              char type, const char *unit,
                              void *minimum, void *maximum)
{
    if (mdev_get_output_by_name(md, name, 0))
        return 0;
    mapper_signal sig = msig_new(name, length, type, 1, unit, minimum,
                                 maximum, 0, 0);
    md->n_outputs++;
    grow_ptr_array((void **) &md->outputs, md->n_outputs,
                   &md->n_alloc_outputs);

    mdev_increment_version(md);

    md->outputs[md->n_outputs - 1] = sig;
    sig->device = md;
    if (md->admin->name)
        sig->props.device_name = md->admin->name;
    return sig;
}

void mdev_remove_input(mapper_device md, mapper_signal sig)
{
    int i, n;
    for (i=0; i<md->n_inputs; i++) {
        if (md->inputs[i] == sig)
            break;
    }
    if (i==md->n_inputs)
        return;

    for (n=i; n<(md->n_inputs-1); n++) {
        md->inputs[n] = md->inputs[n+1];
    }
    md->n_inputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

void mdev_remove_output(mapper_device md, mapper_signal sig)
{
    int i, n;
    for (i=0; i<md->n_outputs; i++) {
        if (md->outputs[i] == sig)
            break;
    }
    if (i==md->n_outputs)
        return;

    for (n=i; n<(md->n_outputs-1); n++) {
        md->outputs[n] = md->outputs[n+1];
    }
    md->n_outputs --;
    mdev_increment_version(md);
    msig_free(sig);
}

int mdev_num_inputs(mapper_device md)
{
    return md->n_inputs;
}

int mdev_num_outputs(mapper_device md)
{
    return md->n_outputs;
}

mapper_signal mdev_get_input_by_name(mapper_device md, const char *name,
                                     int *index)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_inputs; i++)
    {
        if (strcmp(md->inputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (index)
                *index = i;
            return md->inputs[i];
        }
    }
    return 0;
}

mapper_signal mdev_get_output_by_name(mapper_device md, const char *name,
                                      int *index)
{
    int i;
    int slash = name[0]=='/' ? 1 : 0;
    for (i=0; i<md->n_outputs; i++)
    {
        if (strcmp(md->outputs[i]->props.name + 1,
                   name + slash)==0)
        {
            if (index)
                *index = i;
            return md->outputs[i];
        }
    }
    return 0;
}

mapper_signal mdev_get_input_by_index(mapper_device md, int index)
{
    if (index >= 0 && index < md->n_inputs)
        return md->inputs[index];
    return 0;
}

mapper_signal mdev_get_output_by_index(mapper_device md, int index)
{
    if (index >= 0 && index < md->n_outputs)
        return md->outputs[index];
    return 0;
}

int mdev_poll(mapper_device md, int block_ms)
{
    int admin_count = mapper_admin_poll(md->admin);
    int count = 0;

    if (md->server) {

        /* If a timeout is specified, loop until the time is up. */
        if (block_ms)
        {
            double then = get_current_time();
            int left_ms = block_ms;
            while (left_ms > 0)
            {
                if (lo_server_recv_noblock(md->server, left_ms))
                    count++;
                double elapsed = get_current_time() - then;
                left_ms = block_ms - (int)(elapsed*1000);
            }
        }

        /* When done, or if non-blocking, check for remaining messages
         * up to a proportion of the number of input
         * signals. Arbitrarily choosing 1 for now, since we don't
         * support "combining" multiple incoming streams, so there's
         * no point.  Perhaps if this is supported in the future it
         * can be a heuristic based on a recent number of messages per
         * channel per poll. */
        while (count < md->n_inputs*1
               && lo_server_recv_noblock(md->server, 0))
            count++;
    }
    else if (block_ms)
        usleep(block_ms * 1000);

    return admin_count + count;
}

void mdev_route_signal(mapper_device md, mapper_signal sig,
                       mapper_signal_value_t *value)
{
    int i;
    mapper_router r = md->routers;
    for (i = 0; i < sig->props.length; i++) {
        sig->value[i] = value[i];
    }
    sig->has_value = 1;
    while (r) {
        mapper_router_receive_signal(r, sig, value);
        r = r->next;
    }
}

void mdev_add_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    rt->next = *r;
    *r = rt;
}

void mdev_remove_router(mapper_device md, mapper_router rt)
{
    mapper_router *r = &md->routers;
    while (*r) {
        if (*r == rt) {
            *r = rt->next;
            break;
        }
        r = &(*r)->next;
    }
}

/*! Called when once when the port is allocated and again when the
 *  ordinal is allocated, or vice-versa.  Must start server when both
 *  have been allocated. (No point starting it earlier since we won't
 *  be able to register any handlers. */
void mdev_on_port_and_ordinal(mapper_device md,
                              mapper_admin_allocated_t *resource)
{
    if (!(md->admin->ordinal.locked && md->admin->port.locked))
        return;

    trace
        ("device '%s.%d' acknowledged port and ordinal allocation for %d\n",
         md->name_prefix, md->admin->ordinal.value, md->admin->port.value);

    mdev_start_server(md);
}

/* Note: any call to liblo where get_liblo_error will be called
 * afterwards must lock this mutex, otherwise there is a race
 * condition on receiving this information.  Could be fixed by the
 * liblo error handler having a user context pointer. */
static int liblo_error_num = 0;
static void liblo_error_handler(int num, const char *msg, const char *path)
{
    liblo_error_num = num;
    if (num == LO_NOPORT) {
        trace("liblo could not start a server because port unavailable\n");
    } else
        fprintf(stderr, "[libmapper] liblo server error %d in path %s: %s\n",
               num, path, msg);
}

static int get_liblo_error()
{
    int num = liblo_error_num;
    liblo_error_num = 0;
    return num;
}

#ifdef HAVE_PTHREAD
static pthread_mutex_t liblo_error_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Use these functions to handle the liblo error mutex. (Avoids having
 * to sprinkle ifdefs for HAVE_PTHREAD everywhere.) */
static void lock_liblo_error_mutex()
{
#ifdef HAVE_PTHREAD
    pthread_mutex_lock(&liblo_error_mutex);
#endif
}

static void unlock_liblo_error_mutex()
{
#ifdef HAVE_PTHREAD
    pthread_mutex_unlock(&liblo_error_mutex);
#endif
}

void mdev_start_server(mapper_device md)
{
    if (md->n_inputs > 0 && md->admin->port.locked && !md->server) {
        int i, j;
        char port[16], *type = 0;

        sprintf(port, "%d", md->admin->port.value);

        lock_liblo_error_mutex();
        md->server = lo_server_new(port, liblo_error_handler);

        if (md->server) {
            trace("device '%s' opened server on port %d\n",
                  mapper_admin_name(md->admin), md->admin->port.value);

        } else {
            trace("error opening server on port %d for device '%s'\n",
                  md->admin->port.value, md->name_prefix);
            if (get_liblo_error() == LO_NOPORT) {
                md->admin->port.value++;
                md->admin->port.locked = 0;
                mapper_admin_port_probe(md->admin);
            }
            unlock_liblo_error_mutex();
            return;
        }
        unlock_liblo_error_mutex();

        for (i = 0; i < md->n_inputs; i++) {
            type = (char*) realloc(type, md->inputs[i]->props.length + 1);
            for (j = 0; j < md->inputs[i]->props.length; j++)
                type[j] = md->inputs[i]->props.type;
            type[j] = 0;
            lo_server_add_method(md->server,
                                 md->inputs[i]->props.name,
                                 type,
                                 handler_signal, (void *) (md->inputs[i]));
        }
        free(type);
    }
}

const char *mdev_name(mapper_device md)
{
    /* Hand this off to the admin struct, where the name may be
     * cached. However: manually checking ordinal.locked here so that
     * we can safely trace bad usage when mapper_admin_full_name is
     * called inappropriately. */
    if (md->admin->ordinal.locked)
        return mapper_admin_name(md->admin);
    else
        return 0;
}

unsigned int mdev_port(mapper_device md)
{
    if (md->admin->port.locked)
        return md->admin->port.value;
    else
        return 0;
}

const struct in_addr *mdev_ip4(mapper_device md)
{
    if (md->admin->port.locked)
        return &md->admin->interface_ip;
    else
        return 0;
}

const char *mdev_interface(mapper_device md)
{
    return md->admin->interface;
}

unsigned int mdev_ordinal(mapper_device md)
{
    if (md->admin->ordinal.locked)
        return md->admin->ordinal.value;
    else
        return 0;
}

int mdev_ready(mapper_device device)
{
    if (!device)
        return 0;

    return device->admin->registered;
}

void mdev_set_property(mapper_device dev, const char *property,
                       lo_type type, lo_arg *value)
{
    mapper_table_add_or_update_osc_value(dev->extra,
                                         property, type, value);
}

void mdev_remove_property(mapper_device dev, const char *property)
{
    table_remove_key(dev->extra, property, 1);
}
