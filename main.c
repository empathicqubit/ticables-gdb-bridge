#include <tilp2/ticables.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <error.h>
#include <unistd.h>
#include <signal.h>

static CableDeviceInfo EmptyInfo;

static pthread_t tid;

static CableHandle* handle;

static pthread_mutex_t lock;

static CablePort port;

void reset_cable(void) {
    unsigned char err;
    ticables_cable_reset(handle);
    ticables_cable_close(handle);
    ticables_handle_del(handle);
    handle = ticables_handle_new(CABLE_SLV, port);
    ticables_options_set_delay(handle, 1);
    ticables_options_set_timeout(handle, 5);

    while(err = ticables_cable_open(handle)) {
        fprintf(stderr, "Could not open cable: %d\n", err);
    }
}

typedef enum {
    LEVEL_ERROR,
    LEVEL_WARN,
    LEVEL_INFO,
    LEVEL_DEBUG,
    LEVEL_TRACE,
} LOG_LEVEL;

static LOG_LEVEL current_log_level = LEVEL_TRACE;

#define log(level, fmt, values...) if(level <= current_log_level) { fprintf(stderr, fmt, ## values); fflush(stderr); }

void retry_send(unsigned char* send, int sendCount) {
    unsigned char err = 0;
    log(LEVEL_DEBUG, "SENDING %d BYTES \n", sendCount);
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
    log(LEVEL_DEBUG, "RECEIVED %d BYTES\n", recvCount);
    log(LEVEL_TRACE, "%.*s\n", recvCount, recv)
    int c = 0;
    while((c += fwrite(&recv[c], 1, recvCount - c, stdout)) < recvCount);
    fflush(stdout);
}

void INThandler(int sig) {
    signal(sig, SIG_IGN);
    char rest[1024];
    int count = read(0, rest, sizeof(rest));
    fwrite(rest, count, 1, stderr);
    fflush(stderr);
}

int main(void) {
    int **cables = NULL;
    int err;

/*
    signal(SIGINT, INThandler);
    signal(SIGSTOP, INThandler);
    signal(SIGTERM, INThandler);
    signal(SIGQUIT, INThandler);
    */

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    log(LEVEL_INFO, "PROCESS ID: %d\n", getpid());

    if (pthread_mutex_init(&lock, NULL) != 0) {
        fprintf(stderr, "mutex init has failed\n");
        return 1;
    }

    ticables_library_init();

	// search for all USB cables (faster)
	log(LEVEL_INFO, "Searching for link cables...\n");
	err = ticables_probing_do(&cables, 5, PROBE_USB | PROBE_FIRST);
	if(err)
	{
        log(LEVEL_ERROR, "Could not probe cable: %d\n", err);
		ticables_probing_finish(&cables);
		return 1;
	}

    int *ports = cables[CABLE_SLV];
    int i;
    for(i = 0; !ports[i] && i < 5; i++);

    port = ports[i];
    handle = ticables_handle_new(CABLE_SLV, port);
    ticables_options_set_delay(handle, 1);
    ticables_options_set_timeout(handle, 5);

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

    log(LEVEL_INFO, "INFO: Family %d, Variant %d\n", info.family, info.variant);

    bool handle_acks = true;
    bool handled_first_recv = false;

    while(true) {
        unsigned char recv[1023];
        unsigned char current = 0;
        int recvCount = 0;
        int c;

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
