#include <glib.h>
#include <syslog.h>
#include <signal.h>

#include "ptz.h"
#include "param.h"
#include "vip.h"


//#define LOG(fmt, args...)   { syslog(LOG_INFO, fmt, ## args); printf(fmt, ## args); }
#define LOG(fmt, args...)   { }
#define ERR(fmt, args...)   { syslog(LOG_ERR, fmt, ## args); printf(fmt, ## args); }
#define APP_ID              "Axvisca"

GMainLoop *loop;
guint declaration;

/**
 * Quit the application when terminate signals is being sent
 */
static void 
handle_sigterm(int signo)
{
    g_main_loop_quit(loop);
}

/*
 * Register callback to SIGTERM and SIGINT signals
 */
static void 
init_signals()
{
    struct sigaction sa;
    sa.sa_flags = 0;

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

int 
main(int argc, char *argv[])
{
    openlog(APP_ID, LOG_PID | LOG_CONS, LOG_USER);
    init_signals();
    loop = g_main_loop_new(NULL, FALSE);

    param_init(APP_ID);

    ptz_init(); 

    if (vip_init() < 0) {
        return -1;
    }

    g_main_loop_run(loop);
    g_main_loop_unref(loop);  
    param_cleanup();
    closelog();

    return 0;
}
