/*
** Copyright (C) 2009-2022 Quadrant Information Security <quadrantsec.com>                                             ** Copyright (C) 2009-2022 Champ Clark III <cclark@quadrantsec.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,                                                     ** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* gzip.c - Reads data in from a gzip log files */

#ifdef HAVE_CONFIG_H
#include "config.h"             /* From autoconf */
#endif

#ifdef HAVE_LIBZ

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>

#include "sagan.h"
#include "sagan-defs.h"
#include "sagan-config.h"
#include "ignore.h"

#include "lockfile.h"
#include "stats.h"

extern struct _SaganCounters *counters;
extern struct _SaganConfig *config;
extern struct _SaganDebug *debug;

extern struct _Sagan_Pass_Syslog *SaganPassSyslog;
extern pthread_cond_t SaganProcDoWork;
extern pthread_mutex_t SaganProcWorkMutex;

extern uint_fast16_t proc_msgslot;
extern uint_fast16_t proc_running;

void GZIP_Input( const char *input_file )
{

    bool ignore_flag = false;

    uint_fast16_t batch_count = 0;

    int i = 0;

    gzFile fd;

    char syslogstring[MAX_SYSLOGMSG] = { 0 };

    struct _Sagan_Pass_Syslog *SaganPassSyslog_LOCAL = NULL;
    SaganPassSyslog_LOCAL = malloc(sizeof(_Sagan_Pass_Syslog));

    if ( SaganPassSyslog_LOCAL == NULL )
        {
            Sagan_Log(ERROR, "[%s, line %d] Failed to allocate memory for SaganPassSyslog_LOCAL. Abort!", __FILE__, __LINE__);
        }

    memset(SaganPassSyslog_LOCAL, 0, sizeof(struct _Sagan_Pass_Syslog));

    SaganPassSyslog = malloc(config->max_processor_threads * sizeof(_Sagan_Pass_Syslog));

    if ( SaganPassSyslog == NULL )
        {
            Sagan_Log(ERROR, "[%s, line %d] Failed to allocate memory for SaganPassSyslog. Abort!", __FILE__, __LINE__);
        }

    memset(SaganPassSyslog, 0, sizeof(struct _Sagan_Pass_Syslog));

    /* Open GZIP file */

    if (( fd = gzopen(input_file, "rb")) == NULL )
        {
            Sagan_Log(WARN, "[%s, line %d] Cannot open %s! [%s]", __FILE__, __LINE__, input_file, strerror(errno));
            return;
        }

    Sagan_Log(NORMAL, "Successfully opened GZIP file %s....  processing events.....", input_file);

    /* Grab data and process */

    while(gzgets(fd, syslogstring, MAX_SYSLOGMSG) != NULL)
        {

            counters->events_received++;

//            if ( batch_count <= config->max_batch )
//                {

            /* Not threaded yet, so no locking needed */

            uint_fast32_t bytes_total = strlen( syslogstring );


            counters->bytes_total = counters->bytes_total + bytes_total;

            if ( bytes_total > counters->max_bytes_length )
                {
                    counters->max_bytes_length = bytes_total;
                }

            if ( bytes_total >= MAX_SYSLOGMSG )
                {
                    counters->max_bytes_over++;
                }


            if (debug->debugsyslog)
                {
                    Sagan_Log(DEBUG, "[%s, line %d] [batch position %d] Raw log: %s",  __FILE__, __LINE__, batch_count, syslogstring);
                }


            /* Drop any stuff that needs to be "ignored" */

            if ( config->sagan_droplist_flag == true )
                {

                    ignore_flag = false;

                    if ( Ignore( syslogstring ) == true )
                        {
                            ignore_flag = true;
                        }

                }

            if ( ignore_flag == false )
                {
                    strlcpy(SaganPassSyslog_LOCAL->syslog[batch_count], syslogstring, sizeof(SaganPassSyslog_LOCAL->syslog[batch_count]));
                    batch_count++;
                }

//                }

            /* If we are out of threads,  we do this.   An empty loop does not
               seem to work!.   We wait for a thread to become available */

            while ( proc_msgslot >= config->max_processor_threads )
                {

                    struct timespec ts = { 0, 0 };
                    nanosleep(&ts, &ts);

                }

            if ( proc_msgslot < config->max_processor_threads )
                {

                    /* Has our batch count been reached */

                    if ( batch_count >= config->max_batch )
                        {

                            batch_count=0;              /* Reset batch/queue */

                            pthread_mutex_lock(&SaganProcWorkMutex);

                            /* Copy local thread data to global thread */

                            for ( i = 0; i < config->max_batch; i++)
                                {
                                    strlcpy(SaganPassSyslog[proc_msgslot].syslog[i], SaganPassSyslog_LOCAL->syslog[i], sizeof(SaganPassSyslog->syslog[i]));

                                }

                            counters->events_processed = counters->events_processed + config->max_batch;

                            proc_msgslot++;

                            pthread_cond_signal(&SaganProcDoWork);
                            pthread_mutex_unlock(&SaganProcWorkMutex);
                        }

                }
            else
                {
                    /* If there's no thread, we lose the entire batch */

                    counters->worker_thread_exhaustion = counters->worker_thread_exhaustion + config->max_batch; ;
                    batch_count = 0;
                }


        }

    gzclose(fd);

}

#endif
