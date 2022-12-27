#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <glib-2.0/glib.h>

#include <tilp2/ticables.h>
#include <tilp2/ticalcs.h>
#include <tilp2/tifiles.h>
#include <tilp2/keys83p.h>

#include "common/utils.h"

static char *subtype = "";
static char *program = "";
static char *keys = "";
static int reset_ram = 0;

static CalcHandle *calc_handle = NULL;
static CalcModel model = CALC_NONE;

static void send_key(uint32_t key, int retry) {
    usleep(100000);
    int err;
    while(err = ticalcs_calc_send_key(calc_handle, key) && retry);
}

static int compare(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static int get_program_index(GNode *tree, char* program) {
    char *names[1024];
    int n = 0;

    TreeInfo *info = tree->data;
    for (int i = 0; i < (int)g_node_n_children(tree); i++) {
        GNode *parent = g_node_nth_child(tree, i);
        VarEntry *ve = parent->data;

        for (int j = 0; j < (int)g_node_n_children(parent); j++) {
            GNode *child = g_node_nth_child(parent, j);
            ve = child->data;

            if(ve != NULL) {
                const char *str_type = tifiles_vartype2string(model, ve->type);

                if (strlen(str_type) >= 4
                    && (
                        strcmp(&str_type[strlen(str_type) - 4], "PRGM") == 0 
                        || strcmp(str_type, "APPL") == 0
                    )
                ) {
                    names[n++] = ve->name;
                }
            }
        }
    }

    qsort(names, n, sizeof(names[0]), compare);
    for(int i = 0; i < n; i++) {
        if(strcmp(names[i], program) == 0) {
            return i+1; // Finance is always at the beginning
        }
    }

    return -1;
}

void show_help() {
    log(LEVEL_INFO, "Syntax: tikeys [--reset-ram] [--keys=ABCDEFG123456789] [--subtype=mirage --program=PROGNAME]\n");
}

int main(int argc, char *argv[]) {
    utils_parse_args(argc, argv);

    const struct option long_opts[] = {
        {"subtype", required_argument, 0, 's'},
        {"program", required_argument, 0, 'p'},
        {"keys", required_argument, 0, 'k'},
        {"reset-ram", no_argument, &reset_ram, 1},
        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    optind = 0;
    int opt_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, "s:p:", long_opts, &opt_index)) != -1) {
        if(opt == 0 && long_opts[opt_index].flag) {
            // Do nothing
        }
        else if(opt == 's') {
            subtype = optarg;
        }
        else if(opt == 'p') {
            program = optarg;
        }
        else if(opt == 'k') {
            keys = optarg;
        }
        else if(opt == 'r') {
            reset_ram = 1;
        }
        else if(opt == 'h') {
            show_help();
            return 0;
        }
    }

    int err;
    ticables_library_init();

    CableHandle *cable_handle = utils_setup_cable();
    if(cable_handle == NULL) {
        log(LEVEL_ERROR, "Cable not found!\n");
        return 1;
    }

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

    err = ticables_cable_close(cable_handle);
    if(err) {
        log(LEVEL_ERROR, "Could not close cable: %d\n", err);
        return 1;
    }

    ticables_handle_del(cable_handle);
    
    cable_handle = utils_setup_cable();

    ticables_options_set_timeout(cable_handle, 20);

    model = CALC_TI83P;

    calc_handle = ticalcs_handle_new(model);
    ticalcs_cable_attach(calc_handle, cable_handle);

    if(reset_ram) {
        send_key(KEY83P_Quit, 1);
        send_key(KEY83P_Clear, 1);

        log(LEVEL_WARN, "Resetting RAM...\n");
        send_key(KEY83P_ResetMem, 0);
    }

    if(strlen(keys) > 0) {
        for(int i = 0; i < strlen(keys); i++) {
            send_key(ticalcs_keys_83p(keys[i])->normal.value, 1);
        }
    }
    
    if(strlen(subtype) > 0 || strlen(program) > 0) {
        send_key(KEY83P_Quit, 1);
        send_key(KEY83P_Clear, 1);

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
        else if(strlen(program) > 0) {
            if(strcmp(subtype, "noshell") != 0) {
                log(LEVEL_WARN, "Subtype was not recognized! It will be started with noshell!\n");
            }

            log(LEVEL_INFO, "Verifying that noshell is correctly hooked.\n");

            int err;
            GNode *vars, *apps;
            while(err = ticalcs_calc_get_dirlist(calc_handle, &vars, &apps));

            log(LEVEL_DEBUG, "Got dirlist\n");

            int noshell_idx = get_program_index(apps, "Noshell ");
            if(noshell_idx == -1) {
                log(LEVEL_ERROR, "Could not find noshell. Exiting\n");
                return 1;
            }

            send_key(KEY83P_AppsMenu, 1);
            for(int i = 0; i < noshell_idx; i++) {
                send_key(KEY83P_Down, 1);
            }
            //send_key(KEY83P_Enter, 0);

            return 1;

            get_program_index(vars, program);

            // There has to be a better way to do this...
            // Select a program then delete the name,
            // keeping only the pgrm token
            send_key(KEY83P_Prgm, 1);
            send_key(KEY83P_Enter, 1);
            send_key(KEY83P_Up, 1);
            send_key(KEY83P_Right, 1);
            for(int i = 0; i < 16; i++) {
                send_key(KEY83P_Del, 1);
            }

            // Actually input the name
            for(int i = 0; i < strlen(program); i++) {
                send_key(ticalcs_keys_83p(program[i])->normal.value, 1);
            }
            send_key(KEY83P_Enter, 0);
        }
    }

    ticalcs_cable_detach(calc_handle);
    ticalcs_handle_del(calc_handle);
    ticables_cable_close(cable_handle);
    ticables_handle_del(cable_handle);
    ticables_library_exit();

    return 0;
}