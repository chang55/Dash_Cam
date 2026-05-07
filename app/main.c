/* app/main.c */
#include "dvr_daemon.h"
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    
    /* 可选：守护进程化 */
    /* daemon(0, 0); */
    
    if (dvr_daemon_init() < 0) {
        return -1;
    }
    
    dvr_daemon_run();
    dvr_daemon_shutdown();
    
    return 0;
}