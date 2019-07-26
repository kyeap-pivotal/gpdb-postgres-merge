
// ReadOneRecord and SimpleXLogPageRead taken from parsexlog.c
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

// kafka send_message and its callback function takenfrom librdkafka
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

#include "access/xlogreader.h"
#include "access/xlogrecord.h"
#include "access/xlog_internal.h"
#include "rmgrdesc.h"

typedef struct JKXLogPrivate
{
    TimeLineID	timeline;
    const char  *segment_buf;
    XLogRecPtr	startptr;
    XLogRecPtr	endptr;
    bool		endptr_reached;
} JKXLogPrivate;

FILE *file;
int total_bytes = 0;
char *seg_buffer;
char filename[MAXFNAMELEN];
XLogReaderState *xlogreader;
static XLogSegNo xlogreadsegno;
static off_t seg_offset = 0;
static off_t seg_flushed = 0;
XLogRecPtr first_record = -1;

static int run = 1;
static rd_kafka_t *rk;
static int exit_eof = 0;
static int wait_eof = 0;  /* number of partitions awaiting EOF */
static int quiet = 0;
static 	enum {
    OUTPUT_HEXDUMP,
    OUTPUT_RAW,
    OUTPUT_QUIET,
} output = OUTPUT_QUIET;


void decode_and_write_message(rd_kafka_message_t *rkmessage);
FILE *open_walfile(char *filename);
void setXLogStartRecPtr(char *);
static int JKReadPage(XLogReaderState *state, XLogRecPtr targetPagePtr, int reqLen,
                 XLogRecPtr targetPtr, char *readBuff, TimeLineID *curFileTLI);
static void JKXLogRead(const char *directory, TimeLineID timeline_id,
                 XLogRecPtr startptr, char *buf, Size count);


void
decode_and_write_message(rd_kafka_message_t *rkmessage)
{
    /* XLog Parsing and Decoding Suite. */

    /* Add consumed messages into a one-segment-sized (16MB) buffer. */
    if (seg_offset + rkmessage->len < XLOG_SEG_SIZE) {
        memcpy(seg_buffer + seg_offset, rkmessage->payload, rkmessage->len);
        seg_offset += rkmessage->len;
    } else {
        // TODO: Deal with moving the other bytes to a new buffer later?
    }

    /* Start decoding. */
    XLogRecord *record;
    char *errormsg;
    const RmgrDescData *desc;

    if (first_record == -1)
    {
        /* Setup first recptr to start reading from */
		first_record = XLogFindNextRecord(xlogreader, ((JKXLogPrivate *) xlogreader->private_data)->startptr);
    }

    /* first_record is initially set to the very first record to read from.
    * Here, it is currently hardcoded to 0/01000028.
    * Subsequently, it will always be InvalidXLogRecPtr because XLogReader
    * will continue reading from its internal last-read pointer. */
	record = XLogReadRecord(xlogreader, first_record, &errormsg);
	if (record != NULL) {
		first_record = InvalidXLogRecPtr;
    }

//    if (record == NULL)
//        // TODO: deal message not being valid anymore
//        return;

    while (record != NULL)
    {
		desc = &RmgrDescTable[record->xl_rmid];
		if (!quiet)
			printf("RMID=%s, lsn=%lX, TOTAL_LEN=%d\n", desc->rm_name, xlogreader->ReadRecPtr, record->xl_tot_len);
		record = XLogReadRecord(xlogreader, first_record, &errormsg);
    }

    printf("seg-off: %lld", seg_offset);

    /* write output to file. */
	if (file != NULL) {
		fwrite(seg_buffer+seg_flushed, seg_offset-seg_flushed, 1, file);
		seg_flushed = seg_offset;
    } else {
        printf("No file given...\n");
    }

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
        memcpy(p, segment_buf+startoff, segbytes);

        /* Update state for read */
        recptr += segbytes;

        sendOff += segbytes;
        nbytes -= segbytes;
        p += segbytes;
    }
}

static void stop (int sig) {
        if (!run)
                exit(1);
	run = 0;
	fclose(stdin); /* abort fgets() */
}

/* Creates a file with name FILENAME and memsets all bytes to 0.
 * Size of the file is exactly XLOG_SEG_SIZE. */
FILE *
open_walfile(char *filename)
{
    char        zerobuf[XLOG_BLCKSZ];
    FILE        *fp;
    int         bytes;

    fp = fopen(filename, "wb");

    /* New, empty, file. So pad it to 16Mb with zeroes */
    memset(zerobuf, 0, XLOG_BLCKSZ);

    for (bytes = 0; bytes < XLOG_SEG_SIZE; bytes += XLOG_BLCKSZ)
    {
        fwrite(zerobuf, XLOG_BLCKSZ, 1, fp);
    }

    fseek(fp, 0, SEEK_SET);

    return fp;
}

/* TODO: This function will parse XLog files in XLOGDIR and find the last XLOG lsn written.
 * The XLogRecPtr that immediately follows the last XLOG written will be set to be
 * the global variable `first_record`, the first XLOG lsn for the xlogreader to read from. */
void setXLogStartRecPtr(char *xlogdir)
{
	/* Setup the initial starting point of XLog receiving.
	 * Currently hardcoded. */
	// XLogSegNoOffsetToRecPtr(xlogreadsegno, seg_offset , private.startptr);

	return;
}


static void hexdump (FILE *fp, const char *name, const void *ptr, size_t len) {
	const char *p = (const char *)ptr;
	unsigned int of = 0;


	if (name)
		fprintf(fp, "%s hexdump (%zd bytes):\n", name, len);

	for (of = 0 ; of < len ; of += 16) {
		char hexen[16*3+1];
		char charen[16+1];
		int hof = 0;

		int cof = 0;
		int i;

		for (i = of ; i < (int)of + 16 && i < (int)len ; i++) {
			hof += sprintf(hexen+hof, "%02x ", p[i] & 0xff);
			cof += sprintf(charen+cof, "%c",
				       isprint((int)p[i]) ? p[i] : '.');
		}
		fprintf(fp, "%08x: %-48s %-16s\n",
			of, hexen, charen);
	}
}

/**
 * Kafka logger callback (optional)
 */
static void logger (const rd_kafka_t *rk, int level,
                    const char *fac, const char *buf) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(stdout, "%u.%03u RDKAFKA-%i-%s: %s: %s\n",
            (int)tv.tv_sec, (int)(tv.tv_usec / 1000),
            level, fac, rd_kafka_name(rk), buf);
}

/**
 * Handle and print a consumed message.
 * Internally crafted messages are also used to propagate state from
 * librdkafka to the application. The application needs to check
 * the `rkmessage->err` field for this purpose.
 */
static void msg_consume (rd_kafka_message_t *rkmessage) {
	if (rkmessage->err) {
		if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
			fprintf(stderr,
				"%% Consumer reached end of %s [%"PRId32"] "
			       "message queue at offset %"PRId64"\n",
			       rd_kafka_topic_name(rkmessage->rkt),
			       rkmessage->partition, rkmessage->offset);

			if (exit_eof && --wait_eof == 0) {
                                fprintf(stderr,
                                        "%% All partition(s) reached EOF: "
                                        "exiting\n");
				run = 0;
                        }

			return;
		}

                if (rkmessage->rkt)
                        fprintf(stderr, "%% Consume error for "
                                "topic \"%s\" [%"PRId32"] "
                                "offset %"PRId64": %s\n",
                                rd_kafka_topic_name(rkmessage->rkt),
                                rkmessage->partition,
                                rkmessage->offset,
                                rd_kafka_message_errstr(rkmessage));
                else
                        fprintf(stderr, "%% Consumer error: %s: %s\n",
                                rd_kafka_err2str(rkmessage->err),
                                rd_kafka_message_errstr(rkmessage));

                if (rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_PARTITION ||
                    rkmessage->err == RD_KAFKA_RESP_ERR__UNKNOWN_TOPIC)
                        run = 0;
		return;
	}

	if (!quiet)
		fprintf(stdout, "%% Message (topic %s [%"PRId32"], "
                        "offset %"PRId64", %zd bytes):\n",
                        rd_kafka_topic_name(rkmessage->rkt),
                        rkmessage->partition,
			rkmessage->offset, rkmessage->len);

	if (output == OUTPUT_HEXDUMP)
		hexdump(stdout, "Message Payload",
			rkmessage->payload, rkmessage->len);
	else if (output == OUTPUT_RAW)
		printf("%.*s\n",
		       (int)rkmessage->len, (char *)rkmessage->payload);


	/* Record total number of bytes consumed. */
    total_bytes += (int) rkmessage->len;

    decode_and_write_message(rkmessage);
}


static void print_partition_list (FILE *fp,
                                  const rd_kafka_topic_partition_list_t
                                  *partitions) {
        int i;
        for (i = 0 ; i < partitions->cnt ; i++) {
                fprintf(stderr, "%s %s [%"PRId32"] offset %"PRId64,
                        i > 0 ? ",":"",
                        partitions->elems[i].topic,
                        partitions->elems[i].partition,
			partitions->elems[i].offset);
        }
        fprintf(stderr, "\n");

}
static void rebalance_cb (rd_kafka_t *rk,
                          rd_kafka_resp_err_t err,
			  rd_kafka_topic_partition_list_t *partitions,
                          void *opaque) {

	fprintf(stderr, "%% Consumer group rebalanced: ");

	switch (err)
	{
	case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
		fprintf(stderr, "assigned:\n");
		print_partition_list(stderr, partitions);
		rd_kafka_assign(rk, partitions);
		wait_eof += partitions->cnt;
		break;

	case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
		fprintf(stderr, "revoked:\n");
		print_partition_list(stderr, partitions);
		rd_kafka_assign(rk, NULL);
		wait_eof = 0;
		break;

	default:
		fprintf(stderr, "failed: %s\n",
                        rd_kafka_err2str(err));
                rd_kafka_assign(rk, NULL);
		break;
	}
}

static void sig_usr1 (int sig) {
	rd_kafka_dump(stdout, rk);
}

int main (int argc, char **argv) {
        char mode = 'C';
	char *brokers = "localhost:9092";
	int opt;
	rd_kafka_conf_t *conf;
	rd_kafka_topic_conf_t *topic_conf;
	char errstr[512];
	char tmp[16];
    rd_kafka_resp_err_t err;
    char *group = NULL;
    rd_kafka_topic_partition_list_t *topics;
    int is_subscription;
    int i;

	/* XLogReader Related stuff. */
	seg_buffer = calloc(1, XLOG_SEG_SIZE);
	JKXLogPrivate private;

	/* Create XLogReaderState. */
	xlogreader = XLogReaderAllocate(&JKReadPage, &private);
	if (xlogreader == NULL)
		// pg_fatal("out of memory\n");
		printf("Out of memory\n");

	/* Setup xlogreader private. */
	private.segment_buf = seg_buffer;

	/* End of XLogReader Related stuff. */


	quiet = !isatty(STDIN_FILENO);

	/* Kafka configuration */
	conf = rd_kafka_conf_new();

    /* Set logger */
    rd_kafka_conf_set_log_cb(conf, logger);

	/* Quick termination */
	snprintf(tmp, sizeof(tmp), "%i", SIGIO);
	rd_kafka_conf_set(conf, "internal.termination.signal", tmp, NULL, 0);

	/* Topic configuration */
	topic_conf = rd_kafka_topic_conf_new();

	while ((opt = getopt(argc, argv, "f:s:g:b:q:HR")) != -1) {
		switch (opt) {
		case 'b':
			brokers = optarg;
			break;
        case 'g':
                group = optarg;
                break;
		case 'f':
			file = open_walfile(optarg); // fopen(optarg, "wb");
			xlogreadsegno = 5L;
			break;
		case 's':
			if (sscanf(optarg, "%lu", &xlogreadsegno) != 1) {
				fprintf(stderr, "failed to get segment number\n");
			}
			XLogFileName(filename, 1, xlogreadsegno);
			file = open_walfile(filename);
			break;
		case 'q':
			quiet = 1;
			break;
		case 'H':
			output = OUTPUT_HEXDUMP;
			break;
        case 'R':
            output = OUTPUT_RAW;
            break;
		default:
			goto usage;
		}
	}

	/* Setup the initial starting point of XLog receiving. */
    XLogSegNoOffsetToRecPtr(xlogreadsegno, seg_offset , private.startptr);

	/* End of XLogReader setup. */

	if (strchr("OC", mode) && optind == argc) {
	usage:
		fprintf(stderr,
			"Usage: %s [options] <topic[:part]> <topic[:part]>..\n"
			"\n"
			"librdkafka version %s (0x%08x)\n"
			"\n"
			" Options:\n"
			"  -f <filename>   output file name. Assumes segment number is 5.\n"
			"  -s <seg_no>     segment number to start reading from (not filename). Sets output filename to segment.\n"
            "  -g <group>      Consumer group (%s)\n"
			"  -b <brokers>    Broker address (%s)\n"

			"  -q              Be quiet\n"
            "  -H              Hexdump payload output (consumer)\n"
			"  -R              Raw payload output (consumer)\n"
                        "For balanced consumer groups use the 'topic1 topic2..'"
                        " format\n"
                        "and for static assignment use "
                        "'topic1:part1 topic1:part2 topic2:part1..'\n"
			"\n",
			argv[0],
			rd_kafka_version_str(), rd_kafka_version(),
                        group, brokers);
		exit(1);
	}

	signal(SIGINT, stop);
	signal(SIGUSR1, sig_usr1);


        /*
         * Client/Consumer group
         */

        if (strchr("CO", mode)) {
                /* Consumer groups require a group id */
                if (!group)
                        group = "rdkafka_consumer_example";
                if (rd_kafka_conf_set(conf, "group.id", group,
                                      errstr, sizeof(errstr)) !=
                    RD_KAFKA_CONF_OK) {
                        fprintf(stderr, "%% %s\n", errstr);
                        exit(1);
                }

                /* Consumer groups always use broker based offset storage */
                if (rd_kafka_topic_conf_set(topic_conf, "offset.store.method",
                                            "broker",
                                            errstr, sizeof(errstr)) !=
                    RD_KAFKA_CONF_OK) {
                        fprintf(stderr, "%% %s\n", errstr);
                        exit(1);
                }

                /* Set default topic config for pattern-matched topics. */
                rd_kafka_conf_set_default_topic_conf(conf, topic_conf);

                /* Callback called on partition assignment changes */
                rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);

                rd_kafka_conf_set(conf, "enable.partition.eof", "true",
                                  NULL, 0);
        }

        /* Create Kafka handle */
        if (!(rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf,
                                errstr, sizeof(errstr)))) {
                fprintf(stderr,
                        "%% Failed to create new consumer: %s\n",
                        errstr);
                exit(1);
        }

        /* Add brokers */
        if (rd_kafka_brokers_add(rk, brokers) == 0) {
                fprintf(stderr, "%% No valid brokers specified\n");
                exit(1);
        }


        /* Redirect rd_kafka_poll() to consumer_poll() */
        rd_kafka_poll_set_consumer(rk);

        topics = rd_kafka_topic_partition_list_new(argc - optind);
        is_subscription = 1;
        for (i = optind ; i < argc ; i++) {
                /* Parse "topic[:part] */
                char *topic = argv[i];
                char *t;
                int32_t partition = -1;

                if ((t = strstr(topic, ":"))) {
                        *t = '\0';
                        partition = atoi(t+1);
                        is_subscription = 0; /* is assignment */
                        wait_eof++;
                }

                rd_kafka_topic_partition_list_add(topics, topic, partition);
        }

        if (is_subscription) {
                fprintf(stderr, "%% Subscribing to %d topics\n", topics->cnt);

                if ((err = rd_kafka_subscribe(rk, topics))) {
                        fprintf(stderr,
                                "%% Failed to start consuming topics: %s\n",
                                rd_kafka_err2str(err));
                        exit(1);
                }
        } else {
                fprintf(stderr, "%% Assigning %d partitions\n", topics->cnt);

                if ((err = rd_kafka_assign(rk, topics))) {
                        fprintf(stderr,
                                "%% Failed to assign partitions: %s\n",
                                rd_kafka_err2str(err));
                }
        }

        while (run) {
                rd_kafka_message_t *rkmessage;

                rkmessage = rd_kafka_consumer_poll(rk, 1000);
                if (rkmessage) {
                        msg_consume(rkmessage);
                        rd_kafka_message_destroy(rkmessage);
                }
        }

    err = rd_kafka_consumer_close(rk);
    if (err)
            fprintf(stderr, "%% Failed to close consumer: %s\n",
                    rd_kafka_err2str(err));
    else
            fprintf(stderr, "%% Consumer closed\n");

    rd_kafka_topic_partition_list_destroy(topics);

    /* Destroy handle */
    rd_kafka_destroy(rk);

	/* Let background threads clean up and terminate cleanly. */
	run = 5;
	while (run-- > 0 && rd_kafka_wait_destroyed(1000) == -1)
		printf("Waiting for librdkafka to decommission\n");
	if (run <= 0)
		rd_kafka_dump(stdout, rk);

	if (file != NULL)
		fclose(file);

	printf("FINAL NUMBER OF BYTES RECEIVED: %d", total_bytes);

	return 0;
}
