/*
 * kafka_pipe_consumer.h
 *
 * kafka_pipe_consumer declarations
 *
 * src/bin/jk_kafka_consumer/kafka_pipe_consumer.h
 */

#include <librdkafka/rdkafka.h>

typedef struct kafka_consumer_connection
{
	rd_kafka_t *rk;
	rd_kafka_topic_partition_list_t *topics;
} kafka_consumer;

// typedef struct message
// {
// 	char *payload;
// 	int len;
// 	rd_kafka_message_t *rkmessage;
// } message;
// 
// message *poll_for_message(kafka_consumer *pipe);
// void destroy_message(message *msg);

void initialize_kafka_consumer_connection(kafka_consumer **, char *, char *);

void destroy_kafka_connection(kafka_consumer *pipe);

