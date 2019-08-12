
/*  ReadOneRecord and SimpleXLogPageRead taken from parsexlog.c */
/*-------------------------------------------------------------------------
 *
 * parsexlog.c
 *	  Functions for reading Write-Ahead-Log
 *
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

/*  kafka send_message and its callback function takenfrom librdkafka */
/*
 * librdkafka - Apache Kafka C library
 *
 * Copyright (c) 2015, Magnus Edenhill
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Apache Kafka high level consumer example program
 * using the Kafka driver from librdkafka
 * (https://github.com/edenhill/librdkafka)
 */

#include "postgres.h"

#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <sys/time.h>
#include <errno.h>
#include <getopt.h>

#include <librdkafka/rdkafka.h>
#include <dirent.h>

#include "access/xlogreader.h"
#include "access/xlogrecord.h"
#include "access/xlog_internal.h"
#include "catalog/pg_control.h"
#include "storage/spin.h"
#include "lib/stringinfo.h"
// #include "utils/timestamp.h"
// #include "storage/fd.h"
// #include "utils/guc.h"

#include "rmgrdesc.h"
#include "kafka_pipe_consumer.h"

#define MAXTOPICSIZE 64
#define MAXBROKERSIZE 32
#define MAXBASEDIRSIZE 256
#define RECOVERY_CONFIG_FILE	"jk_recovery.conf"

/* TODO: This may be needed. */
// #define XLOG_CONSISTENCY_POINT 0xD0

/* TODO: Clean up includes. */
/* TODO: Change all the jk stuff to real names. */

typedef struct JKXLogPrivate
{
	TimeLineID	timeline;
	const char *segment_buf;
	XLogRecPtr	startptr;
	XLogRecPtr	endptr;
	bool		endptr_reached;
}			JKXLogPrivate;

FILE	   *file;
int			total_bytes = 0;
char	   *seg_buffer;
char		filename[MAXFNAMELEN];
XLogReaderState *xlogreader;
static XLogSegNo xlogreadsegno;
static off_t seg_offset = 0;
static off_t seg_flushed = 0;
XLogRecPtr	first_record = -1;
static int	quiet;

/* GPDB config information. */
int			num_segs;
char	  **seg_basedirs;
XLogSegNo  *xlog_start_segnos;	/* Xlog segfile number to start replicating
								 * from for each segment. */

/* Kafka pipe information. */
char	  **seg_topics;
char	   broker[MAXBROKERSIZE];


/*TODO: Lock individual variables down there. probably use a spinlock cuz won't wait too long. */
/* Replication information. */
char	  **cp_recent;			/* List of recently obtained consistency
								 * points by segment. */
char	   *cp_flush;			/* Currently flushable consistency point. */


/* Process information. */
pid_t	   *cpids;

char *staging_dir = "/tmp/staging/";


static enum
{
	OUTPUT_HEXDUMP,
	OUTPUT_RAW,
	OUTPUT_QUIET,
}			output = OUTPUT_QUIET;


/* Decoding functions. */
void		decode_message(int, int *, char *, char *, XLogReaderState *, XLogRecPtr);
static int	JKReadPage(XLogReaderState *state, XLogRecPtr targetPagePtr, int reqLen,
					   XLogRecPtr targetPtr, char *readBuff, TimeLineID *curFileTLI);
static void JKXLogRead(const char *directory, TimeLineID timeline_id,
					   XLogRecPtr startptr, char *buf, Size count);

/* Replication functions. */
FILE	   *open_walfile(char *filename);

/* Processing functions. */

/* misc functions. */
void		handle_sig(int);	/* Signal handler. */
void		run_consumer(int);	/* Child process suite. */


/* Decodes XLogs inside the one segment buffer until it can't. */
/* TODO: should set the consistency point variables. */
void
decode_message(int seg_id, int *counter, char * fname, char *seg_buffer, XLogReaderState *xlogreader, XLogRecPtr first_record)
{
	/* Setup initial iljisojfioawejifojeawif oesbfhv;a. */
	XLogRecord *record;
	char	   *errormsg;
	const RmgrDescData *desc;
	char        staging_name[MAXBASEDIRSIZE];
	sprintf(staging_name, "%s%s:%d:%d", staging_dir, fname, seg_id, *counter);
	FILE       *staging_file;
     // StringInfoData buf;
     // initStringInfo(&buf);


	/* Start Decoding. */

	/*
	 * For more infor on first_record's behavior, check the comment on the
	 * run_consumer function.
	 */
	if (first_record == -1)
		first_record = XLogFindNextRecord(xlogreader, ((JKXLogPrivate *) xlogreader->private_data)->startptr);

	record = XLogReadRecord(xlogreader, first_record, &errormsg);
	if (record != NULL)
		first_record = InvalidXLogRecPtr;
	else
	{
		/* TODO: deal with invalid records. */
	}

	while (record != NULL)
	{
		desc = &RmgrDescTable[record->xl_rmid];


        uint8   info = XLogRecGetInfo(xlogreader) & ~XLR_INFO_MASK;
        if (info == XLOG_CONSISTENCY_POINT)
        {
            fprintf(stdout, "Segment %d consistency point %d reached.\n", seg_id, *counter);
            // resetStringInfo(&buf);
			/* prints out description to stdout. */
			staging_file = open_walfile(staging_name);
			fwrite(seg_buffer, 1, XLOG_SEG_SIZE, staging_file);
			fclose(staging_file);
			fprintf(stdout, "Segment %d wrote staging file to %s.\n", seg_id, staging_name);
            *counter += 1;
        }


		/* For Debugging only. Or else comment out. */
		// printf("RMID=%s, lsn=%lX, TOTAL_LEN=%d\n", desc->rm_name, xlogreader->ReadRecPtr, record->xl_tot_len);

		/* Continuously decode records. */
		record = XLogReadRecord(xlogreader, first_record, &errormsg);
	}

	return;
}


/*
 * XLogReader read_page callback
 */
static int
JKReadPage(XLogReaderState *state, XLogRecPtr targetPagePtr, int reqLen,
		   XLogRecPtr targetPtr, char *readBuff, TimeLineID *curFileTLI)
{
	JKXLogPrivate *private = state->private_data;
	int			count = XLOG_BLCKSZ;

	if (private->endptr != InvalidXLogRecPtr)
	{
		if (targetPagePtr + XLOG_BLCKSZ <= private->endptr)
			count = XLOG_BLCKSZ;
		else if (targetPagePtr + reqLen <= private->endptr)
			count = private->endptr - targetPagePtr;
		else
		{
			private->endptr_reached = true;
			return -1;
		}
	}

	JKXLogRead(private->segment_buf, private->timeline, targetPagePtr,
			   readBuff, count);

	return count;
}

/*
 * Read count bytes from a segment file in the specified directory, for the
 * given timeline, containing the specified record pointer; store the data in
 * the passed buffer.
 */
static void
JKXLogRead(const char *segment_buf, TimeLineID timeline_id,
		   XLogRecPtr startptr, char *buf, Size count)
{
	char	   *p;
	XLogRecPtr	recptr;
	Size		nbytes;

	static uint32 sendOff = 0;

	p = buf;
	recptr = startptr;
	nbytes = count;

	while (nbytes > 0)
	{
		uint32		startoff;
		int			segbytes;

		startoff = recptr % XLogSegSize;

		/* How many bytes are within this segment? */
		if (nbytes > (XLogSegSize - startoff))
			segbytes = XLogSegSize - startoff;
		else
			segbytes = nbytes;

		/* Read contents of segment file into xlogreader buffer. */
		memcpy(p, segment_buf + startoff, segbytes);

		/* Update state for read */
		recptr += segbytes;

		sendOff += segbytes;
		nbytes -= segbytes;
		p += segbytes;
	}
}

/* Creates a file with name FILENAME and memsets all bytes to 0.
 * Size of the file is exactly XLOG_SEG_SIZE. */
FILE *
open_walfile(char *filename)
{
	char		zerobuf[XLOG_BLCKSZ];
	FILE	   *fp;
	int			bytes;

	fp = fopen(filename, "wb");
	fprintf(stdout, "Opened file %s.\n", filename);

	/* New, empty, file. So pad it to 16Mb with zeroes */
	memset(zerobuf, 0, XLOG_BLCKSZ);

	for (bytes = 0; bytes < XLOG_SEG_SIZE; bytes += XLOG_BLCKSZ)
	{
		fwrite(zerobuf, 1, XLOG_BLCKSZ, fp);
	}

	fprintf(stdout, "Zero_paded file %s.\n", filename);

	fseek(fp, 0, SEEK_SET);

	return fp;
}

void
handle_sig(int sig)
{
	/*
	 * TODO: fix this because apparenlty SIGINT doesn't kill the process
	 * correclty even though things were, like, interrupted.
	 */
	/* Interrupt all child processes. */
	int			i;

	for (i = 0; i < num_segs; i++)
		kill(cpids[i], SIGINT);

	/* Exit process. */
	exit(0);
}

/* Starts the main routine for the child process.
 * The child process will create and repeatedly poll from a
 * kafka pipe consumer, and decode incoming messages as WAL
 * records. For each `consistency point` (CP) record that
 * arrives, it will update the CP_RECENT[IDENTIFIER] variable.
 * It will also repeated look at the CP_FLUSH variable to flush
 * its internal WAL Segment buffer up to the offset of CP_FLUSH. */
void
run_consumer(int identifier)
{

	fprintf(stdout, "child routine for segment %d started.\n", identifier);

	/* Setup replication info. */
	/* buffer with size of one XLog Segfile. Uesd for intermediate processing. */
	char	   *seg_buffer = calloc(1, XLogSegSize);

	if (seg_buffer == NULL)
	{
		fprintf(stderr, "out of memory.\n");
		exit(0);
	}

	char		filename[MAXFNAMELEN];	/* xlog segmentfile name. */
	FILE	   *fp;				/* Pointer to currently flushing file. */
	off_t		seg_offset;		/* Current offset for seg_buf write. */
	off_t		seg_flushed;	/* Current offset for which flushed to FP. */
	XLogReaderState *xlogreader;
	XLogSegNo	xlogreadsegno;
	XLogRecPtr	first_record;
	JKXLogPrivate private;
	char	   *basedir = seg_basedirs[identifier];
	char        flushpath[MAXBASEDIRSIZE];
	char        staging_path[MAXBASEDIRSIZE];
	int         counter = 0;

	xlogreader = XLogReaderAllocate(&JKReadPage, &private);
	if (xlogreader == NULL)
	{
		fprintf(stderr, "out of memory.\n");
		exit(0);
	}

	seg_offset = 0;				/* Currently, assume all walfile replication
								 * to start from beginning of segment. */
	private.segment_buf = seg_buffer;
	xlogreadsegno = xlog_start_segnos[identifier];
	XLogFileName(filename, 1, xlogreadsegno);

	/* Create filepath using strcat, can concatenate to basedir assuming there is space.
	 * add an extra forward slash just in case config file doesn't have forward slash. */
	strcpy(flushpath, basedir);
	strcat(flushpath, "/");

    /* Create staging path. */
    strcpy(staging_path, flushpath);

	strcat(flushpath, filename);

    fprintf(stdout, "child routine for segment %d ready to start decoding.\n", identifier);
	/*
	 * Set private.startptr to correct XLogRecPtr to allow correct xlog
	 * decoding.
	 */
	XLogSegNoOffsetToRecPtr(xlogreadsegno, seg_offset, private.startptr);

	/*
	 * FIRST_RECORD is a dummy value that will be set to the XLog SegFile's
	 * first XLog record segment on on the first run of decode_message for the
	 * XLogReader to use. Then, it will be permanently set to
	 * InvalidXLogRecPtr as the XLogReader struct keeps an internal pointer to
	 * continue reading.
	 */
	first_record = -1;

	/* Setup kafka pipe consumer. */
	kafka_consumer *pipe;
	char	   *topic = seg_topics[identifier];

	initialize_kafka_consumer_connection(&pipe, broker, topic);

    fprintf(stdout, "segment %d kafka connection created.\n", identifier);

	/*
	 * Main loop. Poll for message, process the message, then check to flush.
	 */
	while (1)
	{
		/* Poll for message. */
		rd_kafka_message_t *msg;

		msg = rd_kafka_consumer_poll(pipe->rk, 1000);
		if (msg)
		{
			if (msg->err)
			{
				/*
				 * We have reached the topic/partition end. Continue to wait
				 * for the response..
				 */
				if (msg->err == RD_KAFKA_RESP_ERR__PARTITION_EOF)
				{
					rd_kafka_message_destroy(msg);
					continue;
				}
			}

			/*
			 * The below writing, breaking up of decoding a chunk of the
			 * message and checking to flush will not work when we get
			 * messages that need to be written to the next XLog Segfile... :(
			 */

			fprintf(stdout, "%s: received %d bytes\n", topic, (int) msg->len);
			// fprintf(stdout, "%s\n", (char *) msg->payload);

			/* Write the message to the segment buffer. */
			if (seg_offset + msg->len < XLOG_SEG_SIZE)
			{
				memcpy(seg_buffer + seg_offset, msg->payload, msg->len);
				seg_offset += msg->len;
			}
			else
			{
				fprintf(stdout, "Accumulated messages exceeded one segment length.\n");
				/* RIP. */
			}

			/* Process the message.
			 * Will also push to a temporary staging location if a consistency record is found. */
			decode_message(identifier, &counter, filename, seg_buffer, xlogreader, first_record);

			/* TODO: Check to flush. This is where the consistency points are tried. */
			// consistency_flush_check(seg_buffer, )

			/* TODO: This is a temprary hacky solution to flush entire buffer to segfile. */
			// fwrite(seg_buffer, 1, XLOG_SEG_SIZE, fp);
			// fseek(fp, 0, SEEK_SET);
			// seg_flushed = seg_offset;

			/* Clean up the message. */
			rd_kafka_message_destroy(msg);
		}
	}

	/* Clean up the kafka connection. */
	destroy_kafka_connection(pipe);
}

void
print_help(void)
{
	fprintf(stdout,
		"Usage: jk_kafka_orchestrator -configfile <filename> \n"
		"\n"
		"\n"
		" Options:\n"
		"  -h   show help message\n"
		"  -configfile <filename>   configuration file. Formatted as follows:\n"
		"\n"
		"<kafka broker>\n"
		"<number of segments>\n"
		"<segment 0 topic> <segment 0 XLog Segfile number to start streaming from> <segment 0 pg_xlog directory path>\n"
		"<segment 1 topic> <segment 1 XLog Segfile number to start streaming from> <segment 1 pg_xlog directory path>\n"
		"... \n"
		"\n"
		"\n"
		"ex) \n"
		"localhost:9092\n"
		"4\n"
		"master 5 /datadirs/master/pg_xlog\n"
		"master 5 /datadirs/primary1/pg_xlog\n"
		"master 7 /datadirs/primary2/pg_xlog\n"
		"master 5 /datadirs/primary3/pg_xlog\n"
		"\n");
}

// /*
//  * See if there is a recovery config file (jk_recovery.conf), and if so
//  * read in parameters
//  *
//  * The file is parsed using the main configuration parser.
//  */
// void
// readRecoveryConfFile(void) {
// 	FILE	   *fd;
// 	TimeLineID	rtli = 0;
// 	bool		rtliGiven = false;
// 	ConfigVariable *item,
// 			*head = NULL,
// 			*tail = NULL;
// 	bool		recoveryTargetActionSet = false;
// 	char *data;
//
// 	fd = AllocateFile(RECOVERY_CONFIG_FILE, "r");
// 	if (fd == NULL)
// 	{
// 		if (errno == ENOENT)
// 			return;				/* not there, so no archive recovery */
// 		ereport(FATAL,
// 				(errcode_for_file_access(),
// 						errmsg("could not open recovery command file \"%s\": %m",
// 							   RECOVERY_CONFIG_FILE)));
// 	}
//
// 	/*
// 	 * Since we're asking ParseConfigFp() to report errors as FATAL, there's
// 	 * no need to check the return value.
// 	 */
// 	(void) ParseConfigFp(fd, RECOVERY_CONFIG_FILE, 0, FATAL, &head, &tail);
//
// 	FreeFile(fd);
//
// 	for (item = head; item; item = item->next)
// 	{
// 		if (strcmp(item->name, "hi") == 0)
// 		{
// 			data = pstrdup(item->value);
// 			fprintf(stdout, "hi = '%s'", data);
// 		}
// 		else if (strcmp(item->name, "bye") == 0)
// 		{
// 			data = pstrdup(item->value);
// 			fprintf(stdout, "bye = '%s'", data);
// 		}
// 	}
// }

int
main(int argc, char **argv)
{

	/* Auxillary variables. */
	int			i;
	FILE		*conf;

	int opt;
	while ((opt = getopt(argc, argv, "c:")) != -1) {
		switch (opt) {
		case 'c':
			conf = fopen(optarg, "r");
			break;
		case 'h':
			print_help();
			exit(0);
			break;
		default:
			print_help();
			exit(0);
			break;
		}
	}

	/* TODO: use readRecoveryConfFile();. */


	/* Setup variables from configuration file. */

	fscanf(conf, "%s", &broker);
	fprintf(stdout, "kafka broker set to %s\n", broker);

	fscanf(conf, "%d", &num_segs);
	fprintf(stdout, "number of segments set to %d\n", num_segs);

	seg_topics = malloc(sizeof(char *) * num_segs);
	seg_basedirs = malloc(sizeof(char *) * num_segs);
	xlog_start_segnos = malloc(sizeof(XLogSegNo) * num_segs);

	if (seg_topics == NULL || seg_basedirs == NULL || xlog_start_segnos == NULL)
	{
		fprintf(stderr, "Out of memory.");
		exit(0);
	}

	for (i = 0; i < num_segs; i++)
	{
		seg_topics[i] = malloc(sizeof(char) * MAXTOPICSIZE);
		seg_basedirs[i] = malloc(sizeof(char) * MAXBASEDIRSIZE);

		fscanf(conf, "%s %lu %s", seg_topics[i], &xlog_start_segnos[i], seg_basedirs[i]);
		fprintf(stdout, "new segment; topic: %s, startsegno: %lu, basedir: %s\n",
				seg_topics[i], xlog_start_segnos[i], seg_basedirs[i]);
	}

	/* Create list of child pids to be used for cleanup. */
	cpids = malloc(sizeof(pid_t) * num_segs);


	/* TODO: Move this below the for loop? only work it for the orchestrator. */
	signal(SIGINT, handle_sig);

	for (i = 0; i < num_segs; i++)
	{
		pid_t		pid = fork();

		if (pid == -1)
		{
			fprintf(stderr, "Error in creating process.");
			exit(1);
		}
		if (pid == 0)
		{
			/* Move into child suite with identifier i. */
			run_consumer(i);
			return 0;
		}
		else
		{
			/* Save child pid for cleanup. */
			cpids[i] = pid;
			fprintf(stdout, "Created new process with pid: %d\n", pid);
		}
	}

	/* This sleep was introduced because directly lldbing into the orchestrator somehow
	 * stopped any of the child processes to run any polling (presumabely by sending them SIGSTOPS)
	 * So I lldb'd into the sleeping master process instead. */
	// sleep(100);

	/* Main orchestration Loop. */
	int seg_id = -1;
	int cp_num; /* cp from the segment file. */
	int curr_cp = 0; /* What we are expecting. */
	int count = 0;
    char filename_prefix[MAXBASEDIRSIZE];
    char filename_cpy[MAXBASEDIRSIZE];
    char move_dir[MAXBASEDIRSIZE];
    char *staged_files[num_segs];
	char *saveptr;
    for (i = 0; i < num_segs; i++)
    {
        staged_files[i] = malloc(sizeof(char)*MAXBASEDIRSIZE);
    }

    /* Open staging directory to look for files to push. */
    DIR *d;
    struct dirent *dir;
    while (1)
    {
        d = opendir(staging_dir);
        if (d)
        {
			count = 0;
            while ((dir = readdir(d)) != NULL)
            {
                if(strcmp(dir->d_name, ".") == 0 ||
                    strcmp(dir->d_name, "..") == 0)
                    continue;

				strcpy(filename_cpy, dir->d_name);
				strcpy(filename_prefix, strtok_r(filename_cpy, ":", &saveptr));
				seg_id = atoi(strtok_r(NULL, ":", &saveptr));
				cp_num = atoi(strtok_r(NULL, ":", &saveptr));

//                sscanf(dir->d_name, "%s:%d:%d", &filename_prefix,
//                       &seg_id, &cp_num);
                // fprintf(stdout, "Processing seg file: %s\n", dir->d_name);
                if (cp_num == curr_cp)
                {
                    /* Add all the seg files to staged_files. */
                    strncpy(staged_files[seg_id], dir->d_name, strlen(dir->d_name)+1);
                    // fprintf(stdout, "Added file: %s to candidate set\n", staged_files[seg_id]);
                    count++;
                }
            }
            closedir(d);
        }
        if (count == num_segs)
        {
            /* Move the files over to the segment base directory. */

            for (i = 0; i < num_segs; i++)
            {
				/* Create filename to be moved by prepending staging directory. */
				char movename[MAXBASEDIRSIZE];
				strcpy(movename, staging_dir);
				strcat(movename, staged_files[i]);
                sprintf(move_dir, "%s/%s", seg_basedirs[i], filename_prefix);
                fprintf(stdout, "Moving segment %d staged file %s to %s\n", i, staged_files[i], move_dir);
                rename(movename, move_dir);
            }

            curr_cp++;
        }
        sleep(0.1);
    }
	return 0;
}
