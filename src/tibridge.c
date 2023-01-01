#include <tilp2/ticables.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "common/utils.h"

static CableHandle* cable_handle;

void show_help() {
    log(LEVEL_INFO, "Syntax: tibridge [--no-handle-acks|--handle-acks]\n");
    log(LEVEL_INFO,
"--no-handle-acks: By default, ACKs (-/+) will be hidden from the client.\n"
"                  This is necessary for working with z88dk-gdb. If you're\n"
"                  using a better client, you can disable this."
    );
}

int hex(char ch) {
    if ((ch >= 'a') && (ch <= 'f'))
        return (ch - 'a' + 10);
    if ((ch >= '0') && (ch <= '9'))
        return (ch - '0');
    if ((ch >= 'A') && (ch <= 'F'))
        return (ch - 'A' + 10);
    return (-1);
}

char *hex2mem(const char *buf, char *mem, uint32_t count) {
    unsigned char ch;
    for (int i = 0; i < count; i++)
    {
        ch = hex(*buf++) << 4;
        ch = ch + hex(*buf++);
        *(mem++) = (char)ch;
    }
    return (mem);
}

void reset_cable(void) {
    CablePort port;
    CableModel model;
    port = cable_handle->port;
    model = cable_handle->model;
    int err;
    ticables_cable_reset(cable_handle);
    ticables_cable_close(cable_handle);
    ticables_handle_del(cable_handle);
    cable_handle = ticables_handle_new(model, port);
    ticables_options_set_delay(cable_handle, 1);
    ticables_options_set_timeout(cable_handle, 5);

    while((err = ticables_cable_open(cable_handle))) {
        log(LEVEL_ERROR, "Could not open cable: %d\n", err);
    }
}

void retry_write_calc(uint8_t* send, int sendCount) {
    unsigned char err = 0;
    log(LEVEL_DEBUG, "%d->", sendCount);
    log(LEVEL_TRACE, "%.*s\n", sendCount, send);
    while((err = ticables_cable_send(cable_handle, send, sendCount))) {
        log(LEVEL_ERROR, "Error sending: %d", err);
        reset_cable();
    }
}

void ack() {
    retry_write_calc((uint8_t*)"+", 1);
}

void nack() {
    retry_write_calc((uint8_t*)"-", 1);
}

int listenFd = -1;
int connectionFd = -1;

void retry_write_host(uint8_t* recv, int recvCount) {
    if(connectionFd == -1) {
        connectionFd = accept(listenFd, NULL, NULL);
    }

    log(LEVEL_DEBUG, "%d<-", recvCount);
    log(LEVEL_TRACE, "%.*s\n", recvCount, recv)
    int c = 0;
    while(c < recvCount) {
        int s = write(connectionFd, &recv[c], recvCount - c);
        if(s <= 0) {
            close(connectionFd);
            connectionFd = accept(listenFd, NULL, NULL);
            continue;
        }
        c += s;
    }
}

void retry_read_host(void* buf, unsigned int count) {
    if(connectionFd == -1) {
        connectionFd = accept(listenFd, NULL, NULL);
        log(LEVEL_DEBUG, "Accepted connection\n");
    }

    int c = 0;
    while(c < count) {
        int s = read(connectionFd, buf, count - c);
        if(s <= 0) {
            close(connectionFd);
            connectionFd = accept(listenFd, NULL, NULL);
            continue;
        }
        c += s;
    }
}

void cleanup() {
    if(cable_handle) {
        ticables_cable_close(cable_handle);
        ticables_handle_del(cable_handle);
    }
    if(connectionFd != -1) {
        close(connectionFd);
        connectionFd = -1;
    }
    if(listenFd != -1) {
        close(listenFd);
        listenFd = -1;
    }
    ticables_library_exit();
}

void handle_sigint(int code) {
    cleanup();
}

int setup_connection(unsigned int port) {
    char sendBuf[1025];
    struct sockaddr_in serv_addr;
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(sendBuf, 0, sizeof(sendBuf));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv_addr.sin_port = htons(port);

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 1); 

    return listenfd;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    // z88dk-gdb doesn't like the ACKs -/+, so we just hide them
    int handle_acks = 1;
    unsigned int port = 8998;

    utils_parse_args(argc, argv);

    const struct option long_opts[] = {
        {"handle-acks", no_argument, &handle_acks, 1},
        {"no-handle-acks", no_argument, &handle_acks, 0},

        {"port", required_argument, 0, 'p'},

        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    optind = 0;
    int opt_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, ":p:", long_opts, &opt_index)) != -1) {
        if(optarg != NULL && strncmp(optarg, "=", 1) == 0) {
            optarg = &optarg[1];
        }

        if(opt == 0 && long_opts[opt_index].flag) {
            // Do nothing
        }
        else if(optarg != NULL && (strncmp(optarg, "-", 1) == 0)) {
            log(LEVEL_ERROR, "Argument for -%c started with a -: %s\n", opt, optarg);
            show_help();
            return 1;
        }
        else if(opt == 'p') {
            sscanf(optarg, "%u", &port);
        }
        else if(opt == 'h') {
            show_help();
            return 0;
        }
    }

    log(LEVEL_DEBUG, "handle acks: %d\n", handle_acks);
    log(LEVEL_DEBUG, "port: %d\n", port);

    listenFd = setup_connection(port);

    int err;
    ticables_library_init();

    log(LEVEL_INFO, "PROCESS ID: %d\n", getpid());

    cable_handle = utils_setup_cable();
    if(cable_handle == NULL) {
        log(LEVEL_ERROR, "Cable not found!\n");
        return 1;
    }

    //ticables_options_set_timeout(cable_handle , 1 * 60 * 60 * 10);

    err = ticables_cable_open(cable_handle);
    if(err) {
        log(LEVEL_ERROR, "Could not open cable: %d\n", err);
        return 1;
    }

    CableDeviceInfo info;
    err = ticables_cable_get_device_info(cable_handle, &info);
    if(err) {
        log(LEVEL_ERROR, "Could not read device info: %d\n", err);
        return 1;
    }

    log(LEVEL_INFO, "Cable Family %d, Variant %d\n", info.family, info.variant);

    bool handled_first_recv = false;

    while(true) {
        uint8_t recv[1023];
        unsigned char current = 0;
        int recvCount = 0;

        log(LEVEL_INFO, "<");
        log(LEVEL_DEBUG, "RECEIVE PHASE\n");
        while(true) {
            do {
                if((err = ticables_cable_recv(cable_handle, &recv[recvCount], 1))) {
                    log(LEVEL_ERROR, "error receiving: %d\n", err);
                }
            } while(err);
            current = recv[recvCount];
            log(LEVEL_TRACE, "%c", current);
            recvCount++;
            if(current == '#') {
                do {
                    if((err = ticables_cable_recv(cable_handle, &recv[recvCount], 2))) {
                        log(LEVEL_ERROR, "error receiving: %d\n", err);
                    }
                } while(err);
                recvCount += 2;

                recv[recvCount] = '\0';

                if(!handle_acks || handled_first_recv) {
                    retry_write_host(recv, recvCount);
                }
                else {
                    log(LEVEL_DEBUG, "Discarded the first packet\n");
                }

                char *packet_char = strchr((const char*)recv, '$');
                if(packet_char) {
                    packet_char++;

                    if(*packet_char == 'O') {
                        int data_size = (recvCount - 5) / 2;
                        char buf[data_size];
                        hex2mem(&packet_char[1], buf, data_size);
                        log(LEVEL_INFO, "\n%.*s", data_size, buf);
                        recvCount = 0;
                        continue;
                    }
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
                        retry_write_host(recv, recvCount);
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

        log(LEVEL_INFO, ">");
        log(LEVEL_DEBUG, "SEND PHASE\n");
        while(true) {
            uint8_t send[255];
            int sendCount = 0;

            while(true) {
                retry_read_host(&send[sendCount], 1);
                current = send[sendCount];
                sendCount++;
                if(current == '#') {
                    retry_read_host(&send[sendCount], 2);
                    sendCount += 2;
                    break;
                }
                else if(sendCount == 1 && (current == '-' || current == '+')) {
                    break;
                }
            }

            retry_write_calc(send, sendCount);
            break;
        }
    }
}

void utils_parse_args(int argc, char *argv[]);
