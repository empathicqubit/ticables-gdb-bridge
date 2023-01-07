#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <glib-2.0/glib.h>

#include <tilp2/ticables.h>
#include <tilp2/ticalcs.h>
#include <tilp2/tifiles.h>
#include <tilp2/keys83p.h>
#include <stdbool.h>

#include "common/utils.h"

#define CABLE_TIMEOUT 20
#define CABLE_FAST_TIMEOUT 2

static char *subtype = "";
static char *program = "";

static CalcModel model = CALC_TI83P;
static char *model_requested = "";
static char *keys = "";
static int reset_ram = 0;
static int reset_archive_vars = 0;

static char *exists_filename = "";
static char *exists_type = "";
static int exists_version = -1;
static int exists_size = -1;

void show_help() {
    log(LEVEL_INFO, 
"Syntax: tikeys [options]\n"
"\n"
"Options:\n"
"[-m|--model=83p]           choose the connected model. Default: 83p\n"
"[-r|--reset-ram]           reset the RAM (83p and variants)\n"
"[-a|--reset-archive-vars]  reset the archive vars (83p and variants)\n"
"[-k|--keys=AZ09]]          press alphanumeric keys\n"
"[[-s|--subtype=noshell] -p|--program=PROGNAME]\n"
"\n"
"[-e|--exists=FILENAME      return success if the file exists on the calculator\n"
"    [-t|--type=PGRM]       return success only if the type matches\n"
"    [-v|--version=1]       return success only if the file version matches\n"
"    [-z|--size=1]          return success only if the file size matches\n"
"]\n"
    );
}

int get_args(int argc, char *argv[]) {
    const struct option long_opts[] = {
        {"calc", required_argument, 0, 'c'},
        {"reset-ram", no_argument, &reset_ram, 1},
        {"reset-archive-vars", no_argument, &reset_archive_vars, 1},
        {"keys", required_argument, 0, 'k'},
        {"subtype", required_argument, 0, 's'},
        {"program", required_argument, 0, 'p'},

        {"exists", required_argument, 0, 'e'},
        {"type", required_argument, 0, 't'},
        {"version", required_argument, 0, 'v'},
        {"size", required_argument, 0, 'z'},

        {"help", no_argument, 0, 'h'},
        {0,0,0,0}
    };

    optind = 0;
    int opt_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, ":c:rak:s:p:e:t:v:z:h", long_opts, &opt_index)) != -1) {
        if(optarg != NULL && strncmp(optarg, "=", 1) == 0) {
            optarg = &optarg[1];
        }

        if(opt == 0 && long_opts[opt_index].flag) {
            // Do nothing
        }
        else if(optarg != NULL && (strncmp(optarg, "-", 1) == 0)) {
            log(LEVEL_ERROR, "Argument for -%c started with a -: %s\n", opt, optarg);
            show_help();
            return EXIT_FAILURE;
        }
        else if(opt == ':') {
            log(LEVEL_ERROR, "-%s requires an argument\n", optarg);
            show_help();
            return EXIT_FAILURE;
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
        else if(opt == 'a') {
            reset_archive_vars = 1;
        }

        else if(opt == 'e') {
            exists_filename = optarg;
        }
        else if(opt == 't') {
            exists_type = optarg;
        }
        else if(opt == 'v') {
            sscanf(optarg, "%d", &exists_version);
        }
        else if(opt == 'z') {
            sscanf(optarg, "%d", &exists_size);
        }

        else if(opt == 'c') {
            model_requested = optarg;
        }
        else if(opt == 'h') {
            show_help();
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static CalcHandle *calc_handle = NULL;
static CableHandle *cable_handle = NULL;
static const CalcKey* (*keys_func)(uint8_t ascii_code) = NULL;

void cleanup() {
    if(calc_handle) {
        ticalcs_cable_detach(calc_handle);
        ticalcs_handle_del(calc_handle);
    }
    if(cable_handle) {
        ticables_cable_close(cable_handle);
        ticables_handle_del(cable_handle);
    }
    ticables_library_exit();
}

static void send_key(uint32_t key, int retry) {
    usleep(100000);
    int err;
    while((err = ticalcs_calc_send_key(calc_handle, key) && retry));
}

static int compare(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

static int get_program_index(GNode *tree, char* program) {
    char *names[1024];
    int n = 0;

    for (int i = 0; i < (int)g_node_n_children(tree); i++) {
        GNode *parent = g_node_nth_child(tree, i);
        VarEntry *ve = parent->data;

        for (int j = 0; j < (int)g_node_n_children(parent); j++) {
            GNode *child = g_node_nth_child(parent, j);
            ve = child->data;

            if(ve != NULL && ve->name[0] == program[0]) {
                const char *str_type = tifiles_vartype2string(model, ve->type);
                bool is_program = strcmp(&str_type[strlen(str_type) - 4], "PRGM") == 0;
                if (
                    strlen(str_type) >= 4
                    && (
                        is_program || strcmp(str_type, "APPL") == 0
                    )
                ) {
                    names[n++] = ve->name;
                    log(LEVEL_TRACE, "%s\n", names[n-1]);
                }
            }
        }
    }

    log(LEVEL_TRACE, "Sorting\n");

    qsort(names, n, sizeof(names[0]), compare);
    for(int i = 0; i < n; i++) {
        log(LEVEL_TRACE, "%s\n", names[i]);
        if(strcmp(names[i], program) == 0) {
            return i;
        }
    }

    return -1;
}

static VarEntry* get_program(GNode *tree, char* program) {
    for (int i = 0; i < (int)g_node_n_children(tree); i++) {
        GNode *parent = g_node_nth_child(tree, i);
        VarEntry *ve = parent->data;

        for (int j = 0; j < (int)g_node_n_children(parent); j++) {
            GNode *child = g_node_nth_child(parent, j);
            ve = child->data;

            if(ve != NULL && strcmp(ve->name, program) == 0) {
                return ve;
            }
        }
    }

    return NULL;
}

int start_app(GNode *apps, char *app_name, bool is_program) {
    static const CalcModel allowed_models[] = {
        CALC_TI83,
        CALC_TI83P,
        CALC_TI84P,
        CALC_TI84P_USB,
        CALC_TI84PC,
        CALC_TI84PC_USB,
        CALC_TI83PCE_USB,
        CALC_TI84PCE_USB,
        CALC_TI84PT_USB,
    };

    bool is_ti8x = false;
    for(int i = 0; i < sizeof(allowed_models)/sizeof(allowed_models[0]); i++) {
        if(allowed_models[i] == model) {
            is_ti8x = true;
            break;
        }
    }

    // We use my function for apps on TI8x, and the builtin for programs
    if(is_program || !is_ti8x) {
        VarEntry *app = get_program(apps, app_name);
        if(app == NULL) {
            return EXIT_FAILURE;
        }

        if(is_ti8x && strcmp(tifiles_vartype2string(model, app->type), "PPRGM") == 0) {
            // We remap TI8x assembly programs so noshell can do its work
            // it doesn't like the Asm( token
            app->type = tifiles_string2vartype(model, "PRGM");
        }

        ticalcs_calc_execute(calc_handle, app, "");

        return EXIT_SUCCESS;
    }

    int app_idx = get_program_index(apps, app_name);
    if(app_idx == -1) {
        return EXIT_FAILURE;
    }

    if(is_program) {
        send_key(KEY83P_Prgm, 1);
    }
    else {
        send_key(KEY83P_AppsMenu, 1);
    }

    send_key(keys_func(app_name[0])->normal.value, 1);

    for(int i = 0; i < app_idx; i++) {
        send_key(KEY83P_Down, 1);
    }
    send_key(KEY83P_Enter, 0);

    if(is_program) {
        send_key(KEY83P_Enter, 0);
    }

    return EXIT_SUCCESS;
}


void handle_sigint(int code) {
    cleanup();
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    utils_parse_args(argc, argv);

    int err;
    if((err = get_args(argc, argv))) {
        cleanup();
        return EXIT_FAILURE;
    }

    log(LEVEL_TRACE, "Subtype: %s\n", subtype);
    log(LEVEL_TRACE, "Keys: %s\n", keys);
    log(LEVEL_TRACE, "Reset RAM: %d\n", reset_ram);
    log(LEVEL_TRACE, "Model Requested: %s\n", model_requested);

    if(strlen(model_requested) != 0) {
        model = ticalcs_string_to_model(model_requested);
        if(model == CALC_NONE) {
            log(LEVEL_ERROR, "Invalid calculator model\n");
            cleanup();
            return EXIT_FAILURE;
        }
    }

    if(model == CALC_TI83P) {
        keys_func = &ticalcs_keys_83p;
    }

    ticables_library_init();

    cable_handle = utils_setup_cable();
    if(cable_handle == NULL) {
        log(LEVEL_ERROR, "Cable not found!\n");
        cleanup();
        return EXIT_FAILURE;
    }

    err = ticables_cable_open(cable_handle);
    if(err) {
        log(LEVEL_ERROR, "Could not open cable: %d\n", err);
        cleanup();
        return EXIT_FAILURE;
    }

    CableDeviceInfo info;
    err = ticables_cable_get_device_info(cable_handle, &info);
    if(err) {
        log(LEVEL_ERROR, "Could not read device info: %d\n", err);
        cleanup();
        return EXIT_FAILURE;
    }

    err = ticables_cable_close(cable_handle);
    if(err) {
        log(LEVEL_ERROR, "Could not close cable: %d\n", err);
        cleanup();
        return EXIT_FAILURE;
    }

    ticables_handle_del(cable_handle);

    cable_handle = utils_setup_cable();

    ticables_options_set_timeout(cable_handle, CABLE_TIMEOUT);

    calc_handle = ticalcs_handle_new(model);
    ticalcs_cable_attach(calc_handle, cable_handle);

    if(reset_ram) {
        send_key(KEY83P_Quit, 1);
        send_key(KEY83P_Clear, 1);

        log(LEVEL_WARN, "Resetting RAM...\n");
        send_key(KEY83P_ResetMem, 0);
    }

    if(reset_archive_vars) {
        send_key(KEY83P_Quit, 1);
        send_key(KEY83P_Clear, 1);

        log(LEVEL_WARN, "Resetting archive vars...\n");

        send_key(KEY83P_Mem, 1);
        send_key(keys_func('7')->normal.value, 1);
        send_key(KEY83P_Right, 1);
        send_key(keys_func('1')->normal.value, 1);
        send_key(keys_func('2')->normal.value, 0);
    }

    if(strlen(keys) > 0) {
        for(int i = 0; i < strlen(keys); i++) {
            send_key(keys_func(keys[i])->normal.value, 1);
        }
    }

    if(strlen(exists_filename) > 0) {
        log(LEVEL_INFO, "Checking for existence of file %s", exists_filename);

        GNode *lists[2];
        while((err = ticalcs_calc_get_dirlist(calc_handle, &lists[0], &lists[1])));

        for (int l = 0; l < sizeof(lists) / sizeof(lists[0]); l++) {
            for (int i = 0; i < (int)g_node_n_children(lists[l]); i++) {
                GNode *parent = g_node_nth_child(lists[l], i);
                VarEntry *ve = parent->data;

                for (int j = 0; j < (int)g_node_n_children(parent); j++) {
                    GNode *child = g_node_nth_child(parent, j);
                    ve = child->data;

                    if(ve != NULL && strcmp(ve->name, exists_filename) == 0) {
                        log(LEVEL_INFO, "Found file: %s\n", exists_filename);
                        if(exists_version >= 0 && ve->version != exists_version) {
                            log(LEVEL_WARN, "Version didn't match: %d\n", ve->version);
                            continue;
                        }
                        if(exists_size >= 0 && ve->size != exists_size) {
                            log(LEVEL_WARN, "Size didn't match: %d\n", ve->size);
                            continue;
                        }
                        const char* str_type = tifiles_vartype2string(model, ve->type);
                        if(strlen(exists_type) > 0 && strcmp(exists_type, str_type) != 0) {
                            log(LEVEL_WARN, "Type didn't match: %s\n", str_type);
                            continue;
                        }

                        cleanup();
                        return EXIT_SUCCESS;
                    }
                }
            }
        }

        log(LEVEL_ERROR, "File not found: %s\n", exists_filename);
        cleanup();
        return EXIT_FAILURE;
    }
    else if(strlen(subtype) > 0 || strlen(program) > 0) {
        log(LEVEL_DEBUG, "Got a program startup request.\n");

        send_key(KEY83P_Quit, 1);
        send_key(KEY83P_Clear, 1);

        if(strcmp(subtype, "asm") == 0) {
            log(LEVEL_ERROR, "asm is not supported! Start it manually!\n");
            cleanup();
            return EXIT_FAILURE;
        }
        else if(strcmp(subtype, "tse") == 0) {
            log(LEVEL_ERROR, "tse is not supported! Start it manually!\n");
            cleanup();
            return EXIT_FAILURE;
        }
        else if(strcmp(subtype, "ion") == 0) {
            log(LEVEL_WARN, "ion will be started, but you still need to start the program yourself.\n");
            send_key(KEY83P_Prgm, 1);
            send_key(keys_func('A')->normal.value, 1);
            send_key(KEY83P_Enter, 1);
            send_key(KEY83P_Enter, 0);
        }
        else if(strcmp(subtype, "mirage") == 0) {
            log(LEVEL_WARN, "Mirage will be started, but you still need to start the program yourself.\n");

            GNode *vars, *apps;
            while((err = ticalcs_calc_get_dirlist(calc_handle, &vars, &apps)));

            log(LEVEL_DEBUG, "Got dirlist\n");

            if((err = start_app(apps, "MirageOS", 0))) {
                log(LEVEL_ERROR, "Could not start MirageOS. Is it installed?\n");
                cleanup();
                return EXIT_FAILURE;
            }
        }
        else if(strlen(program) > 0) {
            if(strcmp(subtype, "noshell") != 0) {
                log(LEVEL_WARN, "Subtype was not recognized! It will be started with noshell!\n");
            }

            GNode *vars, *apps;
            while((err = ticalcs_calc_get_dirlist(calc_handle, &vars, &apps)));

            log(LEVEL_DEBUG, "Got dirlist\n");

            if(get_program(vars, program) == NULL) {
                log(LEVEL_ERROR, "Could not find %s. Is it installed? Error %d\n", program, err);
                cleanup();
                return EXIT_FAILURE;
            }

            log(LEVEL_INFO, "Verifying that noshell is correctly hooked.\n");

            if((err = start_app(apps, "Noshell ", 0))) {
                log(LEVEL_ERROR, "Could not start Noshell. Is it installed?\n");
                cleanup();
                return EXIT_FAILURE;
            }

            ticables_options_set_timeout(cable_handle, CABLE_FAST_TIMEOUT);
            send_key(keys_func('1')->normal.value, 0);
            send_key(KEY83P_Enter, 0);
            send_key(keys_func('6')->normal.value, 0);
            ticables_options_set_timeout(cable_handle, CABLE_TIMEOUT);

            if((err = start_app(vars, program, 1))) {
                log(LEVEL_ERROR, "Could not start %s. Is it installed? Error %d\n", program, err);
                cleanup();
                return EXIT_FAILURE;
            }

            log(LEVEL_INFO, "Finished sucessfully!\n");
        }
    }

    cleanup();
    return EXIT_SUCCESS;
}
