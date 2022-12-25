#include <tilp2/ticables.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <error.h>
#include <unistd.h>
#include <signal.h>

#include "common/utils.h"

static CableDeviceInfo EmptyInfo;

static pthread_t tid;

static CableHandle* handle;

static CablePort port;
static CableModel model;

void reset_cable(void) {
    unsigned char err;
    ticables_cable_reset(handle);
    ticables_cable_close(handle);
    ticables_handle_del(handle);
    handle = ticables_handle_new(model, port);
    ticables_options_set_delay(handle, 1);
    ticables_options_set_timeout(handle, 5);

    while(err = ticables_cable_open(handle)) {
        fprintf(stderr, "Could not open cable: %d\n", err);
    }
}

void retry_send(unsigned char* send, int sendCount) {
    unsigned char err = 0;
    log(LEVEL_DEBUG, "%d->", sendCount);
    log(LEVEL_TRACE, "%.*s\n", sendCount, send);
    while(err = ticables_cable_send(handle, send, sendCount)) {
        log(LEVEL_ERROR, "Error sending: %d", err);
        reset_cable();
    }
}

void ack() {
    retry_send("+", 1);
}

void nack() {
    retry_send("-", 1);
}

void retry_recv(unsigned char* recv, int recvCount) {
    log(LEVEL_DEBUG, "%d<-", recvCount);
    log(LEVEL_TRACE, "%.*s\n", recvCount, recv)
    int c = 0;
    while((c += fwrite(&recv[c], 1, recvCount - c, stdout)) < recvCount);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    int err;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    utils_parse_args(argc, argv);

    ticables_library_init();

    log(LEVEL_INFO, "PROCESS ID: %d\n", getpid());

    handle = utils_setup_cable();
    if(handle == NULL) {
        log(LEVEL_ERROR, "Cable not found!\n");
        return 1;
    }

    // BEGIN DON'T DO THIS FOR TICALCS
    err = ticables_cable_open(handle);
    if(err) {
        log(LEVEL_ERROR, "Could not open cable: %d\n", err);
        return 1;
    }

    CableDeviceInfo info = EmptyInfo;
    err = ticables_cable_get_device_info(handle, &info);
    if(err) {
        log(LEVEL_ERROR, "Could not read device info: %d\n", err);
        return 1;
    }

    log(LEVEL_INFO, "INFO: Model %d, Port:%d, Family %d, Variant %d\n", model, port, info.family, info.variant);
    // END DON'T DO THIS FOR TICALCS

    bool handle_acks = true;
    bool handled_first_recv = false;

    while(true) {
        unsigned char recv[1023];
        unsigned char current = 0;
        int recvCount = 0;
        int c;

        log(LEVEL_INFO, "<");
        log(LEVEL_DEBUG, "RECEIVE PHASE\n");
        while(true) {
            do {
                if(err = ticables_cable_recv(handle, &recv[recvCount], 1)) {
                    log(LEVEL_ERROR, "error receiving: %d\n", err);
                }
            } while(err);
            current = recv[recvCount];
            recvCount++;
            if(current == '#') {
                do {
                    if(err = ticables_cable_recv(handle, &recv[recvCount], 2)) {
                        log(LEVEL_ERROR, "error receiving: %d\n", err);
                    }
                } while(err);
                recvCount += 2;

                if(!handle_acks || handled_first_recv) {
                    retry_recv(recv, recvCount);
                }
                else {
                    log(LEVEL_DEBUG, "Discarded the first packet\n");
                }

                if(handle_acks) {
                    log(LEVEL_DEBUG, "Injecting an ACK\n");
                    ack();
                    handled_first_recv = true;
                    recvCount = 0;
                    continue;
                }

                recvCount = 0;
                break;
            }
            else if(recvCount == 1) {
                if(current == '-') {
                    if(!handle_acks) {
                        retry_recv(recv, recvCount);
                    }
                    else {
                        log(LEVEL_DEBUG, "Discarding a NACK\n");
                    }
                    recvCount = 0;
                    break;
                }
                else if(current == '+') {
                    continue;
                }
            }
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(0, &set);
        struct timeval timeout = { 1, 0 };

        log(LEVEL_INFO, ">");
        log(LEVEL_DEBUG, "SEND PHASE\n");
        while(true) {
            unsigned char send[255];
            int sendCount = 0;

            while(true) {
                while((c = read(0, &send[sendCount], 1)) <= 0);
                current = send[sendCount];
                sendCount++;
                if(current == '#') {
                    read(0, &send[sendCount], 2);
                    sendCount += 2;
                    break;
                }
                else if(sendCount == 1 && (current == '-' || current == '+')) {
                    break;
                }
            }

            retry_send(send, sendCount);
            break;
        }
    }
}

void utils_parse_args(int argc, char *argv[]);