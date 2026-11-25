/* Adapted from x87sidecar's MIT-licensed tests/coop_handshake.c.
 * Required acceleration for one owned Wine process, before Windows startup. */
#include <libkern/OSCacheControl.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "coop_proto.h"

static void unavailable(void) {
    fputs("A1 acceleration: cooperative startup failed\n", stderr);
    _exit(78);
}

__attribute__((constructor)) static void establishAcceleration(void) {
    char expected[64];
    snprintf(expected, sizeof(expected), "x87sidecar.%ld", (long)getpid());
    const char *name = getenv(X87_COOP_ENV);
    if (!name || strcmp(name, expected)) unavailable();
    if (unsetenv("DYLD_INSERT_LIBRARIES") || unsetenv("A1_X87_READY")) unavailable();
    mach_port_t bootstrap = MACH_PORT_NULL, service = MACH_PORT_NULL, reply = MACH_PORT_NULL;
    thread_t thread = mach_thread_self();
    if (task_get_bootstrap_port(mach_task_self(), &bootstrap) != KERN_SUCCESS ||
        bootstrap_look_up(bootstrap, name, &service) != KERN_SUCCESS ||
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &reply) != KERN_SUCCESS)
        unavailable();
    x87_coop_request_t request = {0};
    request.header.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE) |
                               MACH_MSGH_BITS_COMPLEX;
    request.header.msgh_size = sizeof(request);
    request.header.msgh_remote_port = service;
    request.header.msgh_local_port = reply;
    request.header.msgh_id = X87_COOP_MSGH_ID;
    request.body.msgh_descriptor_count = 2;
    request.task_port.name = mach_task_self();
    request.task_port.disposition = MACH_MSG_TYPE_COPY_SEND;
    request.task_port.type = MACH_MSG_PORT_DESCRIPTOR;
    request.thread_port.name = thread;
    request.thread_port.disposition = MACH_MSG_TYPE_COPY_SEND;
    request.thread_port.type = MACH_MSG_PORT_DESCRIPTOR;
    if (mach_msg(&request.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(request), 0,
                 MACH_PORT_NULL, 1000, MACH_PORT_NULL) != KERN_SUCCESS) unavailable();
    struct { x87_coop_reply_t reply; mach_msg_trailer_t trailer; char slack[64]; } response = {0};
    if (mach_msg(&response.reply.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(response), reply,
                 10000, MACH_PORT_NULL) != KERN_SUCCESS ||
        response.reply.header.msgh_id != X87_COOP_MSGH_ID + 1 ||
        response.reply.header.msgh_size != sizeof(x87_coop_reply_t)) unavailable();
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t address = response.reply.icache_addr[i], length = response.reply.icache_len[i];
        if (!address || !length || length > 65536 || address > UINT64_MAX - length) unavailable();
        sys_icache_invalidate((void *)(uintptr_t)address, (size_t)length);
    }
    mach_port_mod_refs(mach_task_self(), reply, MACH_PORT_RIGHT_RECEIVE, -1);
    mach_port_deallocate(mach_task_self(), thread);
    mach_port_deallocate(mach_task_self(), service);
    mach_port_deallocate(mach_task_self(), bootstrap);
    if (unsetenv(X87_COOP_ENV) || setenv("A1_X87_READY", "1", 1)) unavailable();
    fprintf(stderr, "A1 acceleration: cooperative x87 engine ready pid=%ld\n", (long)getpid());
}
