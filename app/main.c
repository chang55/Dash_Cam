#include "dvr_daemon.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (dvr_daemon_init() < 0) {
        return 1;
    }

    dvr_daemon_run();
    dvr_daemon_shutdown();
    return 0;
}
