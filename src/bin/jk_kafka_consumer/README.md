# jk_kafka_consumer

0. Setup primary by following the rules https://docs.google.com/document/d/1x3X12_sX9G0BJKlVk051H2xccFLVnKjWOpcKOXzdnEo/edit?ts=5d1509d0#heading=h.9f9c34ue96n5

1. Run `make` and `make install` on `src/bin/jk_kafka_test` and `src/bin/pg_basebackup`
- On MacOS: if there is a compilation failure, first run `brew install librdkafka`.
- Then, on the `Makefile` of both of the above directories, add `CFLAGS += -I/usr/local/Cellar/librdkafka/1.1.0/include/`
	and `LDFLAGS += -lrdkafka`

2. Create an empty sink location, `/tmp/sink`

3. Run `cd /src/bin/jk_kafka_consumer && jk_kafka_test -f [wal_segment_name] gptest`

4. On a different shell, run `pg_receivexlog -D /tmp/sink -p 5432`

5. Run `pg_xlogdump [wal_segment_name]` on the original shell. Verify that the xlog file dumps correctly.
