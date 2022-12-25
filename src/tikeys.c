#include <tilp2/ticables.h>
#include <tilp2/ticalcs.h>
#include "common/utils.h"
#include <readline/readline.h>
#include <tilp2/keys83p.h>
#include <getopt.h>
#include <unistd.h>

static char *subtype = "mirage";

static CalcHandle *calc_handle = NULL;

void send_key(uint32_t key, int retry) {
    usleep(250000);
    int err;
    while(err = ticalcs_calc_send_key(calc_handle, key) && retry);
}

int main(int argc, char *argv[]) {
    utils_parse_args(argc, argv);

    const struct option long_opts[] = {
        //{name,arg,flag,val}
        {"subtype", required_argument, 0, 's'},
        {0,0,0,0}
    };

    optind = 0;
    int opt_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, "s:", long_opts, &opt_index)) != -1) {
        if(opt == 's') {
            subtype = optarg;
        }
    }

    ticables_library_init();

    CableHandle *cable_handle = utils_setup_cable();
    if(cable_handle == NULL) {
        log(LEVEL_ERROR, "Cable not found!\n");
        return 1;
    }

    ticables_options_set_timeout(cable_handle, 20);

    calc_handle = ticalcs_handle_new(CALC_TI83P);
    ticalcs_cable_attach(calc_handle, cable_handle);

    send_key(KEY83P_Quit, 1);
    send_key(KEY83P_Clear, 1);
    
    int err;
    if(strcmp(subtype, "asm") == 0) {
        log(LEVEL_ERROR, "asm is not supported! Start it manually!\n");
        return 1;
    }
    else if(strcmp(subtype, "tse") == 0) {
        log(LEVEL_ERROR, "tse is not supported! Start it manually!\n");
        return 1;
    }
    else if(strcmp(subtype, "ion") == 0) {
        log(LEVEL_WARN, "ion will be started, but you still need to start the program yourself.\n");
        send_key(KEY83P_Prgm, 1);
        send_key(ticalcs_keys_83p('A')->normal.value, 1);
        send_key(KEY83P_Enter, 1);
        send_key(KEY83P_Enter, 0);
    }
    else if(strcmp(subtype, "mirage") == 0) {
        log(LEVEL_WARN, "Mirage will be started, but you still need to start the program yourself.\n");
        send_key(KEY83P_AppsMenu, 1);
        send_key(ticalcs_keys_83p('M')->normal.value, 1);
        send_key(KEY83P_Enter, 0);
    }
    else {
        log(LEVEL_WARN, "Subtype was not recognized! Start it manually!\n");
    }

    ticalcs_cable_detach(calc_handle);
    ticables_cable_close(cable_handle);

    return 0;
}