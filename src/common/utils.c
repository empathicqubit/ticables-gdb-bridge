#include "utils.h"
#include <string.h>
#include <getopt.h>

LOG_LEVEL current_log_level = LEVEL_INFO;

CableHandle* utils_setup_cable() {
	// search for all USB cables (faster)
	log(LEVEL_INFO, "Searching for link cables...\n");
    int **cables = NULL;
	int err = ticables_probing_do(&cables, 5, PROBE_ALL);
	if(err) {
        log(LEVEL_ERROR, "Could not probe cable: %d\n", err);
		ticables_probing_finish(&cables);
		return NULL;
	}

    CableModel model;
    int port;
    for(model = CABLE_NUL; model < CABLE_MAX ; model++) {
        int *ports = cables[model];
        int i;
        for(i = 0; !ports[i] && i < 5; i++);

        port = ports[i];
        if(port) {
            log(LEVEL_DEBUG, "Cable Model: %d, Port: %d\n", model, i);
            break;
        }
    }

    CableHandle *handle = ticables_handle_new(model, port);
    ticables_options_set_delay(handle, 1);
    ticables_options_set_timeout(handle, 5);

    return handle;
}

void utils_parse_args(int argc, char *argv[]) {
    const struct option long_opts[] = {
        {"log-level", required_argument, 0, 'L'},
        {0,0,0,0}
    };

    int opt_index = 0;
    int opt;
    while((opt = getopt_long(argc, argv, "L:", long_opts, &opt_index)) != -1) {
        if(opt == 'L') {
            if(strcmp(optarg, "warn") == 0) {
                current_log_level = LEVEL_WARN;
            }
            else if(strcmp(optarg, "error") == 0) {
                current_log_level = LEVEL_ERROR;
            }
            else if(strcmp(optarg, "debug") == 0) {
                current_log_level = LEVEL_DEBUG;
            }
            else if(strcmp(optarg, "trace") == 0) {
                current_log_level = LEVEL_TRACE;
            }
            else if(strcmp(optarg, "info") == 0) {
                current_log_level = LEVEL_INFO;
            }
        }
    }
}