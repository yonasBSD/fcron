
/*
 * FCRON - periodic command scheduler 
 *
 *  Copyright 2000-2002 Thibault Godouet <fcron@free.fr>
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

 /* $Id: convert-fcrontab.c,v 1.13 2002-10-06 17:10:37 thib Exp $ */

#include "global.h"

/* log.c */
#include "log.h"
#include "subs.h"

char rcs_info[] = "$Id: convert-fcrontab.c,v 1.13 2002-10-06 17:10:37 thib Exp $";

void info(void);
void usage(void);
void convert_file(char *file_name);
char *read_str(FILE *f, char *buf, int max);
void delete_file(cf_t *file);

char  *cdir = FCRONTABS;      /* the dir where are stored users' fcrontabs */

/* needed by log part : */
char *prog_name = NULL;
char foreground = 1;
pid_t daemon_pid = 0;
char debug_opt = 0;
char dosyslog = 1;

void
info(void)
    /* print some informations about this program :
     * version, license */
{
    fprintf(stderr,
	    "convert-fcrontab "VERSION_QUOTED "\n"
	    "Copyright 2000-2002 Thibault Godouet <fcron@free.fr>\n"
	    "This program is free software distributed WITHOUT ANY WARRANTY.\n"
            "See the GNU General Public License for more details.\n"
	);

    exit(EXIT_OK);

}


void
usage()
  /*  print a help message about command line options and exit */
{
    fprintf(stderr, "\nconvert-fcrontab " VERSION_QUOTED "\n\n"
	    "convert-fcrontab -h\n"
	    "convert-fcrontab -V\n"
	    "convert-fcrontab user\n"
	    "  Update the fcrontab of \"user\" to fit the new binary format.\n"
	);
    
    exit(EXIT_ERR);
}


char *
read_str(FILE *f, char *buf, int max)
    /* return a pointer to string read from file f
     * if it is non-zero length */
{
    int i;

    for (i = 0; i < max; i++)
	if ( (buf[i] = fgetc(f)) == '\0')
	    break;
    buf[max-1] = '\0';

    if ( strlen(buf) == 0 )
	return NULL;
    else
	return strdup2(buf);

}


void
delete_file(cf_t *file)
    /* free a file if user_name is not null 
     *   otherwise free all files */
{
    cl_t *line = NULL;
    cl_t *cur_line = NULL;
    env_t *env = NULL;
    env_t *cur_env = NULL;

    
    /* free lines */
    cur_line = file->cf_line_base;
    while ( (line = cur_line) != NULL) {
	cur_line = line->cl_next;
	free(line->cl_shell);
	free(line->cl_mailto);
	free(line->cl_runas);
	free(line);
    }

    /* free env variables */
    cur_env = file->cf_env_base;
    while  ( (env = cur_env) != NULL ) {
	cur_env = env->e_next;
	free(env->e_val);
	free(env);
    }

    /* finally free file itself */
    free(file->cf_user);
    free(file);

}

/* error management */
#define Save_type(FD, TYPE) \
        { \
          if ( save_type(FD, TYPE) != OK ) { \
            error_e("Could not write type : file has not been installed."); \
            close(FD); \
            remove(buf); \
            exit(EXIT_ERR); \
	  } \
        }

#define Save_str(FD, TYPE, STR) \
        { \
          if ( save_str(FD, TYPE, STR) != OK ) { \
            error_e("Could not write str : file has not been installed."); \
            close(FD); \
            remove(buf); \
            exit(EXIT_ERR); \
	  } \
        }

#define Save_strn(FD, TYPE, STR, SIZE) \
        { \
          if ( save_strn(FD, TYPE, STR, SIZE) != OK ) { \
            error_e("Could not write strn : file has not been installed."); \
            close(FD); \
            remove(buf); \
            exit(EXIT_ERR); \
	  } \
        }

#define Save_lint(FD, TYPE, VALUE) \
        { \
          if ( save_lint(FD, TYPE, VALUE) != OK ) { \
            error_e("Could not write lint : file has not been installed."); \
            close(FD); \
            remove(buf); \
            exit(EXIT_ERR); \
	  } \
        }

void
convert_file(char *file_name)
/* this functions is a mix of read_file() from version 1.0.3 and save_file(),
 * so you can read more comments there */
{
    cf_t *file = NULL;
    cl_t *line = NULL;
    env_t *env = NULL;
    FILE *f = NULL;
    int fd;
    struct stat file_stat;
    char buf[LINE_LEN];
    time_t t_save = 0;

    explain("Converting %s's fcrontab ...", file_name);

    Alloc(file, cf_t);
    /* open file */
    if ( (f = fopen(file_name, "r")) == NULL )
	die_e("Could not read %s", file_name);

    if ( fstat(fileno(f), &file_stat) != 0 ) 
	die_e("Could not stat %s", file_name);

    bzero(buf, sizeof(buf));
    
    if (fgets(buf, sizeof(buf), f) == NULL ||
	strncmp(buf, "fcrontab-017\n",
		sizeof("fcrontab-017\n")) != 0) {

	error("File %s is not valid: ignored.", file_name);
	error("Maybe this file has been generated by a too old version "
	      "of fcron ( <= 0.9.4), or is already in the new binary format.");
	error("In this case, you should reinstall it using fcrontab"
	      " (but be aware that you may lose some data as the last "
	      "execution time and date as if you run a fcrontab -z -n).");
	exit(EXIT_ERR);
    }    

    if ((file->cf_user = read_str(f, buf, sizeof(buf))) == NULL)
	die_e("Cannot read user's name");

    if ( fscanf(f, "%ld", (long int *) &t_save) != 1 )
	error("could not get time and date of saving");

    /* read env variables */
    Alloc(env, env_t);
    while( (env->e_val = read_str(f, buf, sizeof(buf))) != NULL ) {
	env->e_next = file->cf_env_base;
	file->cf_env_base = env;
	Alloc(env, env_t);
    }
    /* free last alloc : unused */
    free(env);

    /* read lines */
    Alloc(line, cl_t);
    while ( fread(line, sizeof(cl_t), 1, f) == 1 ) {

	if ((line->cl_shell = read_str(f, buf, sizeof(buf))) == NULL) {
	    error("Line is not valid (empty shell command) : ignored");
	    continue;
	}
	if ((line->cl_runas = read_str(f, buf, sizeof(buf))) == NULL) {
	    error("Line is not valid (empty runas field) : ignored");
	    continue;
	}
	if ((line->cl_mailto = read_str(f, buf, sizeof(buf))) == NULL) {
	    error("Line is not valid (empty mailto field) : ignored");
	    continue;
	}

	line->cl_next = file->cf_line_base;
	file->cf_line_base = line;
	Alloc(line, cl_t);

    }

    free(line);

    fclose(f);

    /* open a temp file in write mode and truncate it */
    strcpy(buf, "tmp_");
    strncat(buf, file_name, sizeof(buf) - sizeof("tmp_") - 1);
    fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC);
    if ( fd == -1 )
	die_e("Could not open %s", buf);

    if ( fchown(fd, file_stat.st_uid, file_stat.st_gid) != 0 )
	die_e("Could not fchown %s", buf);
    
    if ( fchmod(fd, file_stat.st_mode) != 0 )
	die_e("Could not fchmod %s", buf);
    
    
    Save_lint(fd, S_HEADER_T, S_FILEVERSION );
    Save_str(fd, S_USER_T, file->cf_user);
    Save_lint(fd, S_TIMEDATE_T, t_save);

    for (env = file->cf_env_base; env; env = env->e_next)
	Save_str(fd, S_ENVVAR_T, env->e_val);
	
    for (line = file->cf_line_base; line; line = line->cl_next) {

	/* this ones are saved for every lines */
	Save_str(fd, S_SHELL_T, line->cl_shell);
	Save_str(fd, S_RUNAS_T, line->cl_runas);
	Save_str(fd, S_MAILTO_T, line->cl_mailto);
	Save_lint(fd, S_NEXTEXE_T, line->cl_nextexe);
	Save_strn(fd, S_OPTION_T, line->cl_option, 3);

	/* the following are saved only if needed */
	if ( line->cl_numexe )
	    Save_strn(fd, S_NUMEXE_T, &line->cl_numexe, 1);
	if ( is_lavg(line->cl_option) )
	    Save_strn(fd, S_LAVG_T, line->cl_lavg, 3);
	if ( line->cl_until > 0 )
	    Save_lint(fd, S_UNTIL_T, line->cl_until);
	if ( line->cl_nice != 0 )
	    Save_strn(fd, S_NICE_T, &line->cl_nice, 1);
	if ( line->cl_runfreq > 0 ) {
	    Save_lint(fd, S_RUNFREQ_T, line->cl_runfreq);
	    Save_lint(fd, S_REMAIN_T, line->cl_remain);
	}
		     
	if ( is_freq(line->cl_option) ) {
	    /* save the frequency to run the line */
	    Save_lint(fd, S_TIMEFREQ_T, line->cl_timefreq)
		}
	else {
	    /* save the time and date bit fields */
	    Save_strn(fd, S_MINS_T, line->cl_mins, bitstr_size(60));
	    Save_strn(fd, S_HRS_T, line->cl_hrs, bitstr_size(24));
	    Save_strn(fd, S_DAYS_T, line->cl_days, bitstr_size(32));
	    Save_strn(fd, S_MONS_T, line->cl_mons, bitstr_size(12));
	    Save_strn(fd, S_DOW_T, line->cl_dow, bitstr_size(8));
	}

	/* This field *must* be the last of each line */
	Save_type(fd, S_ENDLINE_T);
    }

    close(fd);

    /* everything's ok : we can override the src file safely */
    if ( rename(buf, file_name) != 0 )
	error_e("Could not rename %s to %s", buf, file_name);

    delete_file(file);
}

int
main(int argc, char *argv[])
{
    int c;
    extern char *optarg;
    extern int optind, opterr, optopt;
    char *user_to_update = NULL;

    if ( strrchr(argv[0], '/') == NULL) prog_name = argv[0];
    else prog_name = strrchr(argv[0], '/') + 1;


    /* constants and variables defined by command line */

    while(1) {
	c = getopt(argc, argv, "hV");
	if (c == EOF) break;
	switch (c) {

	case 'V':
	    info(); break;

	case 'h':
	    usage(); break;

	case ':':
	    fprintf(stderr, "(setopt) Missing parameter");
	    usage();

	case '?':
	    usage();

	default:
	    fprintf(stderr, "(setopt) Warning: getopt returned %c", c);
	}
    }

    if (optind >= argc || argc != 2)
	usage();

    user_to_update = strdup2(argv[optind]);

    if (chdir(cdir) != 0)
	die_e("Could not change dir to " FCRONTABS);

    convert_file(user_to_update);

    exit(EXIT_OK);

}

