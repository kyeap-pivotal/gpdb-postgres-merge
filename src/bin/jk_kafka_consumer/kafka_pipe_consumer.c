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
#include "kafka_pipe_consumer.h"


static int run = 1;
static int wait_eof = 0;  /* number of partitions awaiting EOF */

// message *
// poll_for_message(kafka_consumer *pipe)
// {
// 	message *msg = malloc(sizeof(message));
// 	rd_kafka_message_t *rkmessage;
// 
// 	rkmessage = rd_kafka_consumer_poll(pipe->rk, 1000);
// 	if (!rkmessage) {
// 		return NULL;
// 	}
// 
// 	msg->rkmessage = rkmessage;
// 	msg->payload = (char *) rkmessage->payload;
// 	msg->len = (int) rkmessage->len;
// 
// 	return msg;
// }

// void
// destroy_message(message *msg)
// {
// 	rd_kafka_message_destroy(msg->rkmessage);
// 	return;
// }

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

static void
print_partition_list (FILE *fp,
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

static void
rebalance_cb (rd_kafka_t *rk,
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

static void
stop (int sig) {
	if (!run)
		exit(1);
	run = 0;
	fclose(stdin); /* abort fgets() */
}

static void
sig_usr1 (int sig) {
	return;
}

void
initialize_kafka_consumer_connection(kafka_consumer **pipe, char *broker, char *topic)
{
	char *broker_str;
	char *topic_str;

	/* Initialize consumer pipe. */
	*pipe = malloc(sizeof(kafka_consumer));

	/* Arguments error checking. */
	if ((*pipe) == NULL)
	{
		fprintf(stderr, "No kafka_consumer given.");
		goto terminate;
	}

	if (broker == NULL || strcmp(broker, "") == 0)
	{
		fprintf(stderr, "Default broker localhost:9092 used.");
		broker_str = "localhost:9092";
	}
	broker_str = broker;
	
	if (topic == NULL || strcmp(topic, "") == 0) 
	{
		fprintf(stderr, "No topic given.");
		goto terminate;
	}
	topic_str = topic;


	rd_kafka_conf_t *conf;
	rd_kafka_topic_conf_t *topic_conf;
	char errstr[512];
	char tmp[16];
	char *group = NULL;
	rd_kafka_resp_err_t err;
	rd_kafka_topic_partition_list_t *topics;


	/* Kafka and logger setup. */
	conf = rd_kafka_conf_new();
	rd_kafka_conf_set_log_cb(conf, logger);

	/* Quick termination */
	snprintf(tmp, sizeof(tmp), "%i", SIGIO);
	rd_kafka_conf_set(conf, "internal.termination.signal", tmp, NULL, 0);

	/* Topic configuration */
	topic_conf = rd_kafka_topic_conf_new();

	/* Signal handlers. */
	signal(SIGINT, stop);
	signal(SIGUSR1, sig_usr1);

	/* Check if this part is required. */
	/* Setup a consumer group as it is required. */
	group = "kafka_consumer_temporary_group";
	if (rd_kafka_conf_set(conf, "group.id", group,
						  errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) 
	{
		fprintf(stderr, "%% %s\n", errstr);
		goto terminate;
	}

	/* Consumer groups always use broker based offset storage */
	if (rd_kafka_topic_conf_set(topic_conf, "offset.store.method",
								"broker", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK)
	{
		fprintf(stderr, "%% %s\n", errstr);
		goto terminate;
	}

	/* Set default topic config for pattern-matched topics. */
	rd_kafka_conf_set_default_topic_conf(conf, topic_conf);

	/* Callback called on partition assignment changes */
	rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);

	rd_kafka_conf_set(conf, "enable.partition.eof", "true", NULL, 0);

	/* Create Kafka handle */
	if (!((*pipe)->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf,
							errstr, sizeof(errstr))))
	{
		fprintf(stderr, "%% Failed to create new consumer: %s\n", errstr);
		goto terminate;
	}

	/* Add broker */
	if (rd_kafka_brokers_add((*pipe)->rk, broker_str) == 0)
	{
		fprintf(stderr, "%% No valid brokers specified\n");
		goto terminate;
	}


	/* Redirect rd_kafka_poll() to consumer_poll() */
	rd_kafka_poll_set_consumer((*pipe)->rk);

	/* Setup kafka consumer topic. No partitions, single topic. */
	topics = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(topics, topic_str, -1);
	(*pipe)->topics = topics;

	fprintf(stderr, "%% Subscribing to %d topics\n", topics->cnt);
	if ((err = rd_kafka_subscribe((*pipe)->rk, topics))) {
		fprintf(stderr, "%% Failed to start consuming topics: %s\n", rd_kafka_err2str(err));
		goto terminate;
	}


	/* Successfully created kafka consumer. */
	return;

terminate:
	destroy_kafka_connection(*pipe);
	/* Error in creating consumer. */
	fprintf(stderr, "Error in creating pipe consumer.");

	return;
}

void 
destroy_kafka_connection(kafka_consumer *pipe)
{
	rd_kafka_topic_partition_list_t *topics = pipe->topics;

	rd_kafka_resp_err_t err;

    err = rd_kafka_consumer_close(pipe->rk);
    if (err)
		fprintf(stderr, "%% Failed to close consumer: %s\n", rd_kafka_err2str(err));
    else
		fprintf(stderr, "%% Consumer closed\n");

    rd_kafka_topic_partition_list_destroy(topics);

    /* Destroy handle */
    rd_kafka_destroy(pipe->rk);

	/* Let background threads clean up and terminate cleanly. */
	run = 5;
	while (run-- > 0 && rd_kafka_wait_destroyed(1000) == -1)
		printf("Waiting for librdkafka to decommission\n");
	if (run <= 0)
		rd_kafka_dump(stdout, pipe->rk);

	/* Free consumer pipe. */
	free(pipe);
}

