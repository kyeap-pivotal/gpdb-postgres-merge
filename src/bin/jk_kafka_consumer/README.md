# jk_multisite_streaming_replication_prototype.



## How to Run.
0. Make sure you have cloned this repository.

1. Run `make` and `make install` on `src/bin/jk_kafka_consumer` and `src/bin/pg_basebackup`
    - On MacOS: if there is a compilation failure, first run `brew install librdkafka`. Then, to the `Makefile` of both of the above directories, add `CFLAGS += -I/usr/local/Cellar/librdkafka/1.1.0/include/`
	and `LDFLAGS += -lrdkafka`

2. Create a demo cluster by running `make create-demo-cluster` on the base directory of this repository.

3. Run `bash demo_scripts/setup_environment.sh` to setup the demo directories.

4. Run `bash demo_scripts/create_and_start_remote_cluster.sh` to create the remote cluster segments and start them.

5. Run `bash demo_scripts/run_producers.sh` to start the producer processes for the demo cluster.

6. Run `jk_kafka_orchestrator -c demo_scripts/demo_config` to start up the orchestrator and consumer processes.
    -    Modify the demo_config to make sure the path to the remote data directories are setup correctly.

7. Create relations and add data to the original cluster.

8. Create a consistency point on the original cluster.

9. Clean up the producer and consumer and orchestrator processes by running `pkill jk` and `pkill receivexlog`.

10. Run `bash demo_scripts/promote_and_configure_remote_cluster.sh` to kill the original cluster and modify the remote cluster to become a functioning GPDB cluster.

11. Validate that the remote cluster is running by running `psql -p 17432 [dbname]`




## Design.
There are 4 main components to the design: The ***pipes***, the ***producers*** to the pipe, the ***consumers*** to the pipe, and an ***orchestrator*** of the consumers.
1. **Pipe**: One per (original segment)-(remote segment) connection. Implemented through Kafka. Takes in data  at a source and outputs the same data at the target.
2. **Producer**: One per (original segment)-(remote segment) connection. Utilizes a modified version of pg_receivexlog. Whenever there is a change in the GPDB state, WAL records are generated and written to the corresponding pipe by the producer.
3. **Consumer**: One per (original segment)-(remote segment) connection. Each consumer receives a stream of bytes from the pipe, from which it starts decoding as a stream of WAL records. When a consistency point record is found, an intermediate file is created (named `WAL_segment_name:GPDB_seg_number:Consistency_Point_ID`), which is written to a temporary staging location (`/tmp/staging`)
4. **Orchestrator**: A singular process. Repeatedly looks into the staging location to check for files with the same `Consisten_Point_ID`. If there is a set of such files with set size `number of GPDB segments`, pushes the files to the corresponding `GPDB_segment_datadir/pg_xlog` directory.




## Learnings.
This was hard.




## Possible Improvments.
1. Parse the data portion of the consistency point record to recover its timestamp. (Not used because there were unlinked symbol errors...)
2. Instead of creating a temporary file per consistency point found, create a buffer file where you can append and take blocks of data from to write to the actual WAL_segfile in the remote data directories.
3. Don't use `/tmp`.
4. Don't make infinite while loops and create some signal handlers.
5. Write scripts that actually drain the pipes... currently all broken.
6. Don't use files. Use process shared memory.
7. etc.




## Issues and possible further spikes.
1. Creating a `pg_basebackup` with the `-R` flag, and creating a `pg_basebackup` without the `-R` flag and manually creating a `recovery.conf` file with `primary_conninfo=''` somehow still makes the remote segment get streamed WAL records from the original segment.
    - In the case where the backup was made with the `-R` flag, an intermediate `recovery.conf-e` file seems to be made with the `primary_conninfo='original segment'`.
    - In the case where the backup was created without the `-R` flag, no new file is made but the original segment's WAL is still streamed.
    - !!! Possibly check the hba.conf's replication something field to see where this unwanted behavior is coming from.
