#! /bin/bash

# Run the pg_receivexlogs that are necessary for the replication.

# tested on simple gpdb cluster.
# create table foo (a int);
# insert into foo select * from generate_series(1, 10);
# for reference
# Master on xlogsegment 5
# Primary1 on xlogsegment 5
# Primary2 on xlogsegment 5
# Primary3 on xlogsegment 5
# ??????????????????????????????????? ok i guess it changes everytime.

# run receivexlog on background.
# kafka topics will be basenames, namely master, primary1, primary2, primary3
pg_receivexlog -p 15432 -D /tmp/remote_sink/master &
pg_receivexlog -p 25432 -D /tmp/remote_sink/primary1 &
pg_receivexlog -p 25433 -D /tmp/remote_sink/primary2 &
pg_receivexlog -p 25434 -D /tmp/remote_sink/primary3 &


