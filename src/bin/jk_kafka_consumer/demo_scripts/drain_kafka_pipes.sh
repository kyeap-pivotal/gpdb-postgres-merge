#! /bin/bash

# cleanup script to drain from all the producers.

jk_kafka_consumer -f /tmp/trashfile master &
jk_kafka_consumer -f /tmp/trashfile primary1 &
jk_kafka_consumer -f /tmp/trashfile primary2 &
jk_kafka_consumer -f /tmp/trashfile primary3 &
