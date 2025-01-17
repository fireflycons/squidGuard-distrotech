/*
  By accepting this notice, you agree to be bound by the following
  agreements:

  This software product, squidGuard, is copyrighted (C) 1998-2007
  by Christine Kronberg, Shalla Secure Services. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License (version 2) as
  published by the Free Software Foundation.  It is distributed in the
  hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU General Public License (GPL) for more details.

  You should have received a copy of the GNU General Public License
  (GPL) along with this program.
*/

#include "sg.h"

struct Setting *lastSetting = NULL;
struct Setting *Setting = NULL; /* linked list, Calloc */

struct Source *lastSource = NULL;
struct Source *Source = NULL; /* linked list, Calloc */

struct Destination *lastDest = NULL;
struct Destination *Dest = NULL; /* linked list, Calloc */

struct sgRewrite *lastRewrite = NULL;
struct sgRewrite *Rewrite = NULL; /* linked list, Calloc */
struct sgRegExp *lastRewriteRegExec = NULL;

struct Time *lastTime = NULL;
struct Time *Time = NULL; /* linked list, Calloc */

struct LogFileStat *globalErrorLog = NULL;
struct LogFile *globalLogFile = NULL;

struct LogFileStat *lastLogFileStat;
struct LogFileStat *LogFileStat; /* linked list, Calloc */

struct TimeElement *lastTimeElement = NULL;
struct TimeElement *TimeElement = NULL;

struct Acl *lastAcl = NULL;
struct Acl *defaultAcl = NULL;
struct Acl *Acl = NULL; /* linked list, Calloc */
struct AclDest *lastAclDest = NULL;

struct sgRegExp *lastRegExpDest;

struct Source *lastActiveSource;

char **globalArgv;
char **globalEnvp;
int globalDebugTimeDelta = 0;
int globalDebug = 0;
int globalPid = 0;
int globalUpdate = 0;
int passthrough = 0;
int showBar = 0; /* Do not display the progress bar. */
char *globalCreateDb = NULL;
int failsafe_mode = 0;
int sig_hup = 0;
int sig_alrm = 0;
int sgtime = 0;
char *globalLogDir = NULL;
int lineno;
char *progname;

#if __STDC__
int main(int argc,
         char **argv,
         char **envp)
#else
int main(argc, argv, envp)
int argc;
char *argv[];
char *envp[];
#endif
{
    int ch;
    struct SquidInfo squidInfo;
    struct Source *src;
    struct Acl *acl;
    struct timeval start_time, ready_time, stop_time;
    char buf[MAX_BUF];
    char *redirect, tmp[MAX_BUF];
    char *configFile = NULL;
    time_t t;
#if HAVE_SIGACTION
    struct sigaction act;
#endif
    gettimeofday(&start_time, NULL);
    progname = argv[0];
    globalPid = getpid();
    while ((ch = getopt(argc, argv, "hbduPC:t:c:v")) != EOF)
        switch (ch)
        {
        case 'd':
            globalDebug = 1;
            break;
        case 'c':
            configFile = optarg;
            break;
        case 'b':
            showBar = 1;
        case 'C':
            globalCreateDb = optarg;
            break;
        case 'P':
            passthrough = 1;
            break;
        case 'u':
            globalUpdate = 1;
            break;
        case 'v':
            fprintf(stderr, "SquidGuard: %s %s\n", VERSION, db_version(NULL, NULL, NULL));
            exit(0);
            break;
        case 't':
            if ((t = iso2sec(optarg)) == -1)
            {
                fprintf(stderr, "-t dateformat error, should be yyyy-mm-ddTHH:MM:SS\n");
                exit(0);
            }
            if (t < 0)
            {
                fprintf(stderr, "-t date have to after 1970-01-01T01:00:00\n");
                exit(0);
            }
            sgLogError("squidGuard emulating date %s", niso(t));
            globalDebugTimeDelta = t - start_time.tv_sec;
            start_time.tv_sec = start_time.tv_sec + globalDebugTimeDelta;
            break;
        case '?':
        case 'h':
        default:
            usage();
        }
    globalArgv = argv;
    globalEnvp = envp;
    sgSetGlobalErrorLogFile();
    sgReadConfig(configFile);
    sgSetGlobalErrorLogFile();
    sgLogError("squidGuard %s started (%d.%03d)",
               VERSION, start_time.tv_sec, start_time.tv_usec / 1000);
    if (globalUpdate || globalCreateDb != NULL)
    {
        sgLogError("db update done");
        gettimeofday(&stop_time, NULL);
        stop_time.tv_sec = stop_time.tv_sec + globalDebugTimeDelta;
        sgLogError("squidGuard stopped (%d.%03d)", stop_time.tv_sec, stop_time.tv_usec / 1000);
        exit(0);
    }
    sgTimeElementSortEvents();
    sgTimeNextEvent();
#if HAVE_SIGACTION
#ifndef SA_NODEFER
#define SA_NODEFER 0
#endif
    act.sa_handler = sgHandlerSigHUP;
    act.sa_flags = SA_NODEFER | SA_RESTART;
    sigaction(SIGHUP, &act, NULL);
#else
#if HAVE_SIGNAL
    signal(SIGHUP, sgHandlerSigHUP);
#else
#endif
#endif
    gettimeofday(&ready_time, NULL);
    ready_time.tv_sec = ready_time.tv_sec + globalDebugTimeDelta;
    sgLogError("squidGuard ready for requests (%d.%03d)",
               ready_time.tv_sec, ready_time.tv_usec / 1000);
    tmp[MAX_BUF - 1] = '\0';
    while (1)
    {
        while (fgets(buf, MAX_BUF, stdin) != NULL)
        {
            if (sig_hup)
            {
                sgReloadConfig();
            }
            if (failsafe_mode)
            {
                puts("");
                fflush(stdout);
                if (sig_hup)
                {
                    sgReloadConfig();
                }
                continue;
            }
            if (parseLine(buf, &squidInfo) != 1)
            {
                sgLogError("Error parsing squid line: %s", buf);
                puts("");
            }
            else
            {
                src = Source;
                for (;;)
                {
                    strncpy(tmp, squidInfo.src, MAX_BUF - 1);
                    tmp[MAX_BUF - 1] = 0; /* force null termination */
                    globalLogFile = NULL;
                    src = sgFindSource(src, tmp, squidInfo.ident, squidInfo.srcDomain);
                    acl = sgAclCheckSource(src);
                    if ((redirect = sgAclAccess(src, acl, &squidInfo)) == NULL)
                    {
                        if (src == NULL || src->cont_search == 0)
                        {
                            puts("");
                            break;
                        }
                        else if (src->next != NULL)
                        {
                            src = src->next;
                            continue;
                        }
                        else
                        {
                            puts("");
                            break;
                        }
                    }
                    else
                    {
                        if (squidInfo.srcDomain[0] == '\0')
                        {
                            squidInfo.srcDomain[0] = '-';
                            squidInfo.srcDomain[1] = '\0';
                        }
                        if (squidInfo.ident[0] == '\0')
                        {
                            squidInfo.ident[0] = '-';
                            squidInfo.ident[1] = '\0';
                        }
                        fprintf(stdout, "%s %s/%s %s %s\n", redirect, squidInfo.src,
                                squidInfo.srcDomain, squidInfo.ident,
                                squidInfo.method);
                        /* sgLogError("%s %s/%s %s %s\n",redirect,squidInfo.src,squidInfo.srcDomain,squidInfo.ident,squidInfo.method);  */
                        break;
                    }
                } /*for(;;)*/
            }
            fflush(stdout);
            if (sig_hup)
                sgReloadConfig();
        }
#if !HAVE_SIGACTION
#if HAVE_SIGNAL
        if (errno != EINTR)
        {
            gettimeofday(&stop_time, NULL);
            stop_time.tv_sec = stop_time.tv_sec + globalDebugTimeDelta;
            sgLogError("squidGuard stopped (%d.%03d)", stop_time.tv_sec, stop_time.tv_usec / 1000);
            exit(2);
        }
#endif
#else
        gettimeofday(&stop_time, NULL);
        stop_time.tv_sec = stop_time.tv_sec + globalDebugTimeDelta;
        sgLogError("squidGuard stopped (%d.%03d)", stop_time.tv_sec, stop_time.tv_usec / 1000);
        exit(0);
#endif
    }
    exit(0);
}

#if __STDC__
void usage()
#else
void usage()
#endif
{
    fprintf(stderr,
            "Usage: squidGuard [-u] [-C block] [-t time] [-c file] [-v] [-d] [-P]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v          : show version number\n");
    fprintf(stderr, "  -d          : all errors to stderr\n");
    fprintf(stderr, "  -b          : switch on the progress bar when updating the blacklists\n");
    fprintf(stderr, "  -c file     : load alternate configfile\n");
    fprintf(stderr, "  -t time     : specify startup time in the format: yyyy-mm-ddTHH:MM:SS\n");
    fprintf(stderr, "  -u          : update .db files from .diff files\n");
    fprintf(stderr, "  -C file|all : create new .db files from urls/domain files\n");
    fprintf(stderr, "                specified in \"file\".\n");
    fprintf(stderr, "  -P          : do not go into emergency mode when an error with the \n");
    fprintf(stderr, "                blacklists is encountered.\n");

    exit(1);
}
