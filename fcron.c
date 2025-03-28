/*
 * FCRON - periodic command scheduler 
 *
 *  Copyright 2000-2025 Thibault Godouet <fcron@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *  The GNU General Public License can also be found in the file
 *  `LICENSE' that comes with the fcron source distribution.
 */


#include "fcron.h"

#include "database.h"
#include "conf.h"
#include "job.h"
#include "temp_file.h"
#include "fcronconf.h"
#include "select.h"
#include "suspend.h"
#ifdef FCRONDYN
#include "fcrondyn_svr.h"
#endif


void main_loop(void);
void check_signal(void);
void reset_sig_cont(void);
void info(void);
void usage(void);
void print_schedule(void);
void sighup_handler(int x);
void sigterm_handler(int x);
void sigchld_handler(int x);
void sigusr1_handler(int x);
void sigusr2_handler(int x);
void sigcont_handler(int x);
void set_signal_handler(int signal, void (*handler)(int), bool first_install);
void install_signal_handler(int signal, void (*handler)(int));
void reinstall_signal_handler(int signal, void (*handler)(int));
int parseopt(int argc, char *argv[]);
void get_lock(void);
int is_system_reboot(void);
void create_spooldir(char *dir);

/* command line options */

#ifdef FOREGROUND
char foreground = 1;            /* set to 1 when we are on foreground, else 0 */
#else
char foreground = 0;            /* set to 1 when we are on foreground, else 0 */
#endif

time_t first_sleep = FIRST_SLEEP;
const time_t min_sleep_usec = 1000;     /* 1ms -- we won't sleep for less than this time */
time_t save_time = SAVE;
char once = 0;                  /* set to 1 if fcron shall return immediately after running
                                 * all jobs that are due at the time when fcron is started */

/* Get the default locale character set for the mail
 * "Content-Type: ...; charset=" header */
char default_mail_charset[TERM_LEN] = "";

/* used in temp_file() : create it in current dir (normally spool dir) */
char *tmp_path = "";

/* process identity */
pid_t daemon_pid;
mode_t saved_umask;             /* default root umask */
char *prog_name = NULL;
char *orig_tz_envvar = NULL;

/* uid/gid of user/group root 
 * (we don't use the static UID or GID as we ask for user and group names
 * in the configure script) */
uid_t rootuid = 0;
gid_t rootgid = 0;

/* have we got a signal ? */
bool sig_chld = false;          /* true when we received a SIGCHLD */
bool sig_cont = false;          /* true when we received a SIGCONT */
bool sig_hup = false;           /* true when we received a SIGHUP */
bool sig_term = false;          /* true when we received a SIGTERM */
bool sig_usr1 = false;          /* true when we received a SIGUSR1 */
bool sig_usr2 = false;          /* true when we received a SIGUSR2 */

/* jobs database */
struct cf_t *file_base;         /* point to the first file of the list */
struct job_t *queue_base;       /* ordered list of normal jobs to be run */
unsigned long int next_id;      /* id for next line to enter database */

struct cl_t **serial_array;     /* ordered list of job to be run one by one */
short int serial_array_size;    /* size of serial_array */
short int serial_array_index;   /* the index of the first job */
short int serial_num;           /* number of job being queued */
short int serial_running;       /* number of running serial jobs */

/* do not run more than this number of serial job simultaneously */
short int serial_max_running = SERIAL_MAX_RUNNING;
short int serial_queue_max = SERIAL_QUEUE_MAX;

lavg_list_t *lavg_list;         /* jobs waiting for a given system load value */
short int lavg_queue_max = LAVG_QUEUE_MAX;
short int lavg_serial_running;  /* number of serialized lavg job being running */

exe_list_t *exe_list;           /* jobs which are executed */

time_t now;                     /* the current time */

#ifdef HAVE_LIBPAM
pam_handle_t *pamh = NULL;
const struct pam_conv apamconv = { NULL };
#endif

void
info(void)
    /* print some informations about this program :
     * version, license */
{
    fprintf(stderr,
            "fcron " VERSION_QUOTED " - periodic command scheduler\n"
            "Copyright " COPYRIGHT_QUOTED " Thibault Godouet <fcron@free.fr>\n"
            "This program is free software distributed WITHOUT ANY WARRANTY.\n"
            "See the GNU General Public License for more details.\n");

    exit(EXIT_OK);

}


void
usage(void)
  /*  print a help message about command line options and exit */
{
    fprintf(stderr, "\nfcron " VERSION_QUOTED "\n\n"
            "fcron [-d] [-f] [-b]\n"
            "fcron -h\n"
            "  -s t   --savetime t     Save fcrontabs on disk every t sec.\n"
            "  -l t   --firstsleep t   Sets the initial delay before any job is executed"
            ",\n                          default to %d seconds.\n"
            "  -m n   --maxserial n    Set to n the max number of running serial jobs.\n"
            "  -c f   --configfile f   Make fcron use config file f.\n"
            "  -n d   --newspooldir d  Create d as a new spool directory.\n"
            "  -f     --foreground     Stay in foreground.\n"
            "  -b     --background     Go to background.\n"
            "  -y     --nosyslog       Don't log to syslog at all.\n"
            "  -p     --logfilepath    If set, log to the file given as argument.\n"
            "  -o     --once           Execute all jobs that need to be run, wait for "
            "them,\n                          then return. Sets firstsleep to 0.\n"
            "                          Especially useful with -f and -y.\n"
            "  -d     --debug          Set Debug mode.\n"
            "  -h     --help           Show this help message.\n"
            "  -V     --version        Display version & infos about fcron.\n",
            FIRST_SLEEP);

    exit(EXIT_ERR);
}


void
print_schedule(void)
    /* print the current schedule on syslog */
{
    cf_t *cf;
    cl_t *cl;
    struct tm *ftime;

    explain("Printing schedule ...");
    for (cf = file_base; cf; cf = cf->cf_next) {
        explain(" File %s", cf->cf_user);
        for (cl = cf->cf_line_base; cl; cl = cl->cl_next) {
            ftime = localtime(&(cl->cl_nextexe));
            explain("  cmd '%s' next exec %04d-%02d-%02d wday:%d %02d:%02d"
                    " (system time)",
                    cl->cl_shell, (ftime->tm_year + 1900), (ftime->tm_mon + 1),
                    ftime->tm_mday, ftime->tm_wday, ftime->tm_hour,
                    ftime->tm_min);

        }
    }
    explain("... end of printing schedule.");
}


void
xexit(int exit_value)
    /* exit after having freed memory and removed lock file */
{
    cf_t *f = NULL;

    now = my_time();

    /* we save all files now and after having waiting for all
     * job being executed because we might get a SIGKILL
     * if we don't exit quickly */
    save_file(NULL);

#ifdef FCRONDYN
    fcrondyn_socket_close(NULL);
#endif

    f = file_base;
    while (f != NULL) {
        if (f->cf_running > 0) {
            /* */
            debug("waiting jobs for %s ...", f->cf_user);
            /* */
            wait_all(&f->cf_running);
            save_file(f);
        }
        delete_file(f->cf_user);

        /* delete_file remove the f file from the list :
         * next file to remove is now pointed by file_base. */
        f = file_base;
    }

    remove(pidfile);

    exe_list_destroy(exe_list);
    lavg_list_destroy(lavg_list);
    free_conf();

    Free_safe(orig_tz_envvar);

    explain("Exiting with code %d", exit_value);
    exit(exit_value);

}

void
get_lock()
    /* check if another fcron daemon is running with the same config (-c option) :
     * in this case, die. if not, write our pid to /var/run/fcron.pid in order to lock,
     * and to permit fcrontab to read our pid and signal us */
{
    int otherpid = 0;
    FILE *daemon_lockfp = NULL;
    int fd;

    if (((fd = open(pidfile, O_RDWR | O_CREAT, 0644)) == -1)
        || ((daemon_lockfp = fdopen(fd, "r+"))) == NULL)
        die_e("can't open or create %s", pidfile);

#ifdef HAVE_FLOCK
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
#else                           /* HAVE_FLOCK */
    if (lockf(fileno(daemon_lockfp), F_TLOCK, 0) != 0)
#endif                          /* ! HAVE_FLOCK */
    {
        if (fscanf(daemon_lockfp, "%d", &otherpid) >= 1)
            die_e("can't lock %s, running daemon's pid may be %d", pidfile,
                  otherpid);
        else
            die_e("can't lock %s, and unable to read running"
                  " daemon's pid", pidfile);
    }

    fcntl(fd, F_SETFD, 1);

    rewind(daemon_lockfp);
    fprintf(daemon_lockfp, "%d\n", (int)daemon_pid);
    fflush(daemon_lockfp);
    if (ftruncate(fileno(daemon_lockfp), ftell(daemon_lockfp)) < 0)
        error_e
            ("Unable to ftruncate(fileno(daemon_lockfp), ftell(daemon_lockfp))");

    /* abandon fd and daemon_lockfp even though the file is open. we need to
     * keep it open and locked, but we don't need the handles elsewhere.
     */

}

int
is_system_startup(void)
{
    int reboot = 0;

    /* lock exist - skip reboot jobs */
    if (access(REBOOT_LOCK, F_OK) == 0) {
        explain("@reboot jobs will only be run at computer's startup.");
        /* don't run @reboot jobs */
        return 0;
    }
    /* lock doesn't exist - create lock, run reboot jobs */
    if ((reboot = creat(REBOOT_LOCK, S_IRUSR & S_IWUSR)) < 0)
        error_e("Can't create lock for reboot jobs.");
    else
        xclose_check(&reboot, REBOOT_LOCK);

    /* run @reboot jobs */
    return 1;
}


int
parseopt(int argc, char *argv[])
  /* set options */
{

    int c;
    int i;

#ifdef HAVE_GETOPT_LONG
    static struct option opt[] = {
        {"debug", 0, NULL, 'd'},
        {"foreground", 0, NULL, 'f'},
        {"background", 0, NULL, 'b'},
        {"nosyslog", 0, NULL, 'y'},
        {"logfilepath", 1, NULL, 'p'},
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
        {"once", 0, NULL, 'o'},
        {"savetime", 1, NULL, 's'},
        {"firstsleep", 1, NULL, 'l'},
        {"maxserial", 1, NULL, 'm'},
        {"configfile", 1, NULL, 'c'},
        {"newspooldir", 1, NULL, 'n'},
        {"queuelen", 1, NULL, 'q'},
        {0, 0, 0, 0}
    };
#endif                          /* HAVE_GETOPT_LONG */

    extern char *optarg;
    extern int optind, opterr, optopt;

    /* constants and variables defined by command line */

    while (1) {
#ifdef HAVE_GETOPT_LONG
        c = getopt_long(argc, argv, "dfbyp:hVos:l:m:c:n:q:", opt, NULL);
#else
        c = getopt(argc, argv, "dfbyp:hVos:l:m:c:n:q:");
#endif                          /* HAVE_GETOPT_LONG */
        if (c == EOF)
            break;
        switch ((char)c) {

        case 'V':
            info();
            break;

        case 'h':
            usage();
            break;

        case 'd':
            debug_opt = 1;
            break;

        case 'f':
            foreground = 1;
            break;

        case 'b':
            foreground = 0;
            break;

        case 'y':
            dosyslog = 0;
            break;

        case 'p':
            logfile_path = strdup2(optarg);
            break;

        case 'o':
            once = 1;
            first_sleep = 0;
            break;

        case 's':
            if ((save_time = strtol(optarg, NULL, 10)) < 60
                || save_time >= TIME_T_MAX)
                die("Save time can only be set between 60 and %d.", TIME_T_MAX);
            break;

        case 'l':
            if ((first_sleep = strtol(optarg, NULL, 10)) < 0
                || first_sleep >= TIME_T_MAX)
                die("First sleep can only be set between 0 and %d.",
                    TIME_T_MAX);
            break;

        case 'm':
            if ((serial_max_running = strtol(optarg, NULL, 10)) <= 0
                || serial_max_running >= SHRT_MAX)
                die("Max running can only be set between 1 and %d.", SHRT_MAX);
            break;

        case 'c':
            Set(fcronconf, optarg);
            break;

        case 'n':
            create_spooldir(optarg);
            break;

        case 'q':
            if ((lavg_queue_max = serial_queue_max =
                 strtol(optarg, NULL, 10)) < 5 || serial_queue_max >= SHRT_MAX)
                die("Queue length can only be set between 5 and %d.", SHRT_MAX);
            break;

        case ':':
            error("(parseopt) Missing parameter");
            usage();

        case '?':
            usage();

        default:
            warn("(parseopt) Warning: getopt returned %c", c);
        }
    }

    if (optind < argc) {
        for (i = optind; i <= argc; i++)
            error("Unknown argument \"%s\"", argv[i]);
        usage();
    }

    return OK;

}

void
create_spooldir(char *dir)
    /* create a new spool dir for fcron : set correctly its mode and owner */
{
    int dir_fd = -1;
    struct stat st;
    uid_t useruid = get_user_uid_safe(USERNAME);
    gid_t usergid = get_group_gid_safe(GROUPNAME);

    if (mkdir(dir, 0) != 0 && errno != EEXIST)
        die_e("Cannot create dir %s", dir);

    if ((dir_fd = open(dir, 0)) < 0)
        die_e("Cannot open dir %s", dir);

    if (fstat(dir_fd, &st) != 0) {
        xclose_check(&dir_fd, "spooldir");
        die_e("Cannot fstat %s", dir);
    }

    if (!S_ISDIR(st.st_mode)) {
        xclose_check(&dir_fd, "spooldir");
        die("%s exists and is not a directory", dir);
    }

    if (fchown(dir_fd, useruid, usergid) != 0) {
        xclose_check(&dir_fd, "spooldir");
        die_e("Cannot fchown dir %s to %s:%s", dir, USERNAME, GROUPNAME);
    }

    if (fchmod
        (dir_fd,
         S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP) != 0) {
        xclose_check(&dir_fd, "spooldir");
        die_e("Cannot change dir %s's mode to 770", dir);
    }

    xclose_check(&dir_fd, "spooldir");

    exit(EXIT_OK);

}

void
sigterm_handler(int x)
  /* exit safely */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page). */
    sig_term = true;
}

void
sighup_handler(int x)
  /* update configuration */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page).
     * In particular, we don't call the synchronize_dir() function directly,
     * because it may cause some problems if this signal is not received during
     * the sleep */
    sig_hup = true;
}

void
sigchld_handler(int x)
  /* call wait_chld() to take care of finished jobs */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page). */
    sig_chld = true;
}


void
sigusr1_handler(int x)
  /* reload all configurations */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page).
     * In particular, we don't call the synchronize_dir() function directly,
     * because it may cause some problems if this signal is not received during
     * the sleep */
    sig_usr1 = true;
}


void
sigusr2_handler(int x)
  /* print schedule and switch on/off debug mode */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page). */
    sig_usr2 = true;
}

void
sigcont_handler(int x)
  /* used to notify fcron of a system resume after suspend.
   * However this signal could also be received in other cases. */
{
    /* Defer any work outside of the handler, by fear of inadvertently calling
     * a non-async-signal safe function (see the signal-safety(7) man page). */
    sig_cont = true;
}

void
set_signal_handler(int signal, void (*handler)(int), bool first_install)
    /* (re)install a signal handler, with restartable syscalls retried. */
{
/* *INDENT-OFF* */
#ifdef HAVE_SIGACTION
    /* The signal handler stays set after the handler is called when set
     * with sigaction(): we only need to install it once. */
    if (first_install) {
        struct sigaction act = { 0 };
        act.sa_flags = SA_RESTART;
        act.sa_handler = handler;
        if (sigaction(signal, &act, NULL) < 0) {
            die_e("sigaction() failed on signal %d", signal);
        }
    }
#elif defined(HAVE_SIGNAL)
    /* Some systems reset the handler to SIG_DFL when the handler
     * is called when the handler was set with signal(). So we have to install
     * it (again) every time. */
    if (signal(signal, handler) == SIG_ERR) {
        die_e("signal() failed on signal %d", signal);
    }
    if (siginterrupt(signal, 0) < 0) {
        die_e("siginterrupt() failed on signal %d", signal);
    }
#elif defined(HAVE_SIGSET)
    /* The signal handler stays set after the handler is called when set
     * with sigset(): we only need to install it once. */
    if (first_install) {
        if (sigset(signal, handler) == -1) {
            die_e("sigset() failed on signal %d", signal);
        }
    }
#else
#error "No signal installation function found"
#endif
/* *INDENT-ON* */
}

void
install_signal_handler(int signal, void(*handler)(int))
{
    set_signal_handler(signal, handler, true);
}

void
reinstall_signal_handler(int signal, void (*handler)(int))
    /* reinstall the signal handler, after execution, if needed. */
{
    set_signal_handler(signal, handler, false);
}

int
main(int argc, char **argv)
{
    char *codeset = NULL;

    rootuid = get_user_uid_safe(ROOTNAME);
    rootgid = get_group_gid_safe(ROOTGROUP);

    /* we set it to 022 in order to get a pidfile readable by fcrontab
     * (will be set to 066 later) */
    saved_umask = umask(022);

    /* parse options */

    if (strrchr(argv[0], '/') == NULL)
        prog_name = argv[0];
    else
        prog_name = strrchr(argv[0], '/') + 1;

    {
        uid_t daemon_uid;
        if ((daemon_uid = getuid()) != rootuid)
            die("Fcron must be executed as root");
    }

    /* we have to set daemon_pid before the fork because it's
     * used in die() and die_e() functions */
    daemon_pid = getpid();

    /* save the value of the TZ env variable (used for option timezone) */
    orig_tz_envvar = strdup2(getenv("TZ"));

    parseopt(argc, argv);

    /* read fcron.conf and update global parameters */
    read_conf();

    /* initialize the logs before we become a daemon */
    xopenlog();

    /* change directory */

    if (chdir(fcrontabs) != 0)
        die_e("Could not change dir to %s", fcrontabs);

    /* Get the default locale character set for the mail
     * "Content-Type: ...; charset=" header */
    setlocale(LC_ALL, "");      /* set locale to system defaults or to
                                 * that specified by any  LC_* env vars */
    /* Except that "US-ASCII" is preferred to "ANSI_x3.4-1968" in MIME,
     * even though "ANSI_x3.4-1968" is the official charset name. */
    if ((codeset = nl_langinfo(CODESET)) != 0L &&
        strcmp(codeset, "ANSI_x3.4-1968") != 0)
        strncpy(default_mail_charset, codeset, sizeof(default_mail_charset));
    else
        strcpy(default_mail_charset, "US-ASCII");

    if (freopen("/dev/null", "r", stdin) == NULL)
        error_e("Could not open /dev/null as stdin");

    if (foreground == 0) {

        /* close stdout and stderr.
         * close unused descriptors
         * optional detach from controlling terminal */

        int fd;
        pid_t pid;

        switch (pid = fork()) {
        case -1:
            die_e("fork");
            break;
        case 0:
            /* child */
            break;
        default:
            /* parent */
/*  	    printf("%s[%d] " VERSION_QUOTED " : started.\n", */
/*  		   prog_name, pid); */
            exit(0);
        }

        daemon_pid = getpid();

        if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
#ifndef _HPUX_SOURCE
            ioctl(fd, TIOCNOTTY, 0);
#endif
            xclose_check(&fd, "/dev/tty");
        }

        if (freopen("/dev/null", "w", stdout) == NULL)
            error_e("Could not open /dev/null as stdout");
        if (freopen("/dev/null", "w", stderr) == NULL)
            error_e("Could not open /dev/null as stderr");

        /* close most other open fds */
        xcloselog();
        for (fd = 3; fd < 250; fd++)
            /* don't use xclose_check() as we do expect most of them to fail */
            (void)close(fd);

        /* finally, create a new session */
        if (setsid() == -1)
            error("Could not setsid()");

    }

    /* check if another fcron daemon is running, create pid file and lock it */
    get_lock();

    /* this program belongs to root : we set default permission mode
     * to  600 for security reasons, but we reset them to the saved
     * umask just before we run a job */
    umask(066);

    explain("%s[%d] " VERSION_QUOTED " started", prog_name, daemon_pid);

    install_signal_handler(SIGTERM, sigterm_handler);
    install_signal_handler(SIGHUP, sighup_handler);
    install_signal_handler(SIGCHLD, sigchld_handler);
    install_signal_handler(SIGUSR1, sigusr1_handler);
    install_signal_handler(SIGUSR2, sigusr2_handler);
    install_signal_handler(SIGCONT, sigcont_handler);
    /* we don't want SIGPIPE to kill fcron, and don't need to handle it as when ignored
     * write() on a pipe closed at the other end will return EPIPE */
    install_signal_handler(SIGPIPE, SIG_IGN);

    /* initialize job database */
    next_id = 0;

    /* initialize exe_array */
    exe_list = exe_list_init();

    /* initialize serial_array */
    serial_running = 0;
    serial_array_index = 0;
    serial_num = 0;
    serial_array_size = SERIAL_INITIAL_SIZE;
    serial_array =
        alloc_safe(serial_array_size * sizeof(cl_t *), "serial_array");

    /* initialize lavg_array */
    lavg_list = lavg_list_init();
    lavg_list->max_entries = lavg_queue_max;
    lavg_serial_running = 0;

    /* initialize random number generator :
     * WARNING : easy to guess !!! */
    /* we use the hostname and tv_usec in order to get different seeds
     * on two different machines starting fcron at the same moment */
    {
        char hostname[50];
        int i;
        unsigned int seed;
#ifdef HAVE_GETTIMEOFDAY
        struct timeval tv;      /* we use usec field to get more precision */
        gettimeofday(&tv, NULL);
        seed = ((unsigned int)tv.tv_usec) ^ ((unsigned int)tv.tv_sec);
#else
        seed = (unsigned int)time(NULL);
#endif
        gethostname(hostname, sizeof(hostname));

        for (i = 0; i < sizeof(hostname) - sizeof(seed); i += sizeof(seed))
            seed ^= (unsigned int)*(hostname + i);

        srand(seed);
    }

    main_loop();

    /* never reached */
    return EXIT_OK;
}


void
check_signal()
    /* Action any received signals. */
{
    if (sig_chld == true) {
        wait_chld();
        sig_chld = false;
        reinstall_signal_handler(SIGCHLD, sigchld_handler);
    }

    if (sig_term == true) {
        sig_term = false;
        debug("");
        explain("SIGTERM signal received");
        xexit(EXIT_OK);
    }

    if (sig_hup == true) {
        /* update configuration */
        synchronize_dir(".", 0);
        sig_hup = false;
        reinstall_signal_handler(SIGHUP, sighup_handler);
    }
    if (sig_usr1 == true) {
        /* reload all configuration */
        reload_all(".");
        sig_usr1 = false;
        reinstall_signal_handler(SIGUSR1, sigusr1_handler);
    }

    if (sig_usr2 == true) {
        print_schedule();
        debug_opt = (debug_opt > 0) ? 0 : 1;
        explain("debug_opt = %d", debug_opt);
        sig_usr2 = false;
        reinstall_signal_handler(SIGUSR2, sigusr2_handler);
    }

}

void
reset_sig_cont(void)
   /* reinitialize the SIGCONT handler if it was raised */
{
    if (sig_cont == true) {
        sig_cont = false;
        reinstall_signal_handler(SIGCONT, sigcont_handler);
    }
}

#ifdef HAVE_GETTIMEOFDAY
#define debug_print_tstamp(where_str) \
        { \
        if (debug_opt) { \
            gettimeofday(&now_tv, NULL); \
            debug(where_str ": now=%ld now_tv sec=%ld usec=%ld", now, now_tv.tv_sec, \
                  now_tv.tv_usec); \
        } \
    }
#else
#define debug_print_tstamp(where_str) { ; }
#endif

void
main_loop()
  /* main loop - get the time to sleep until next job execution,
   *             sleep, and then test all jobs and execute if needed. */
{
    time_t save;                /* time remaining until next save */
    time_t slept_from;          /* time it was when we went into sleep */
    time_t nwt;                 /* next wake time */
#ifdef HAVE_GETTIMEOFDAY
    struct select_instance main_select;
    struct timeval now_tv;      /* we use usec field to get more precision */
    struct timeval sleep_tv;    /* we use usec field to get more precision */
#endif

    debug("Entering main loop");

#ifdef HAVE_GETTIMEOFDAY
    select_init(&main_select);
#ifdef FCRONDYN
    fcrondyn_socket_init(&main_select);
#endif
    init_suspend(&main_select);
#endif

    now = my_time();

    synchronize_dir(".", is_system_startup());

    /* synchronize save with jobs execution */
    save = now + save_time;

    if (serial_num > 0 || once) {
        nwt = now + first_sleep;
    }
    else {
        /* force first execution after first_sleep sec : execution of jobs
         * during system boot time is not what we want */
        nwt = tmax(next_wake_time(save), now + first_sleep);
    }
    debug("initial wake time : %s", ctime(&nwt));

    for (;;) {

        /* remember when we started to sleep -- this is to detect suspend/hibernate */
        slept_from = time(NULL);

#ifdef HAVE_GETTIMEOFDAY
        gettimeofday(&now_tv, NULL);
        debug("now gettimeofday tv_sec=%ld, tv_usec=%ld %s", now_tv.tv_sec,
              now_tv.tv_usec, ctime(&nwt));

        if (nwt <= now_tv.tv_sec) {
            sleep_tv.tv_sec = 0;
            sleep_tv.tv_usec = 0;
        }
        else {
            /* compensate for any time spent in the loop,
             * so as we wake up exactly at the beginning of the second */
            sleep_tv.tv_sec = nwt - now_tv.tv_sec - 1;
            /* we set tv_usec to slightly more than necessary so as we don't wake
             * up too early (e.g. due to rounding to the system clock granularity),
             * in which case we would have to go back to sleep again */
            sleep_tv.tv_usec = 1000000 + min_sleep_usec - now_tv.tv_usec;
        }

        if (sleep_tv.tv_usec > 999999) {
            /* On some systems (BSD, etc), tv_usec cannot be greater than 999999 */
            sleep_tv.tv_usec = 999999;
        }
        else if (sleep_tv.tv_sec == 0 && sleep_tv.tv_usec < min_sleep_usec) {
            /* to prevent any infinite loop, sleep at least 1ms */
            debug
                ("We'll sleep for a tiny bit to avoid any risk of infinite loop");
            sleep_tv.tv_usec = min_sleep_usec;
        }
        debug("nwt=%s, sleep sec=%ld, usec=%ld", ctime(&nwt), sleep_tv.tv_sec,
              sleep_tv.tv_usec);
        select_call(&main_select, &sleep_tv);
#else                           /* HAVE_GETTIMEOFDAY */
        if (nwt - now > 0) {
            sleep(nwt - now);
        }
#endif                          /* HAVE_GETTIMEOFDAY */

        debug("\n");
        now = my_time();
        debug_print_tstamp("just woke up");

        check_signal();
        check_suspend(slept_from, nwt, &sig_cont, &main_select);
        reset_sig_cont();
        debug_print_tstamp("    after check_signal and suspend");

        test_jobs();
        debug_print_tstamp("    after test_jobs");

        while (serial_num > 0 && serial_running < serial_max_running) {
            run_serial_job();
        }

        if (once) {
            explain("Running with option once : exiting ... ");
            xexit(EXIT_OK);
        }

        if (save <= now) {
            save = now + save_time;
            /* save all files */
            save_file(NULL);
        }
        debug_print_tstamp("    after save");
#if defined(FCRONDYN) && defined(HAVE_GETTIMEOFDAY)
        /* check if there's a new connection, a new command to answer, etc ... */
        /* we do that *after* other checks, to avoid Denial Of Service attacks */
        fcrondyn_socket_check(&main_select);
#endif

        nwt = check_lavg(save);
        debug_print_tstamp("    after check_lavg");
        debug("next wake time : %s", ctime(&nwt));

        check_signal();

    }

}
