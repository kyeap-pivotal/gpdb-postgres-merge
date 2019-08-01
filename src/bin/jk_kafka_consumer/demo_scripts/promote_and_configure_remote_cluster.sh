#! /bin/bash

# Promote the remote cluster to its own cluster, then configure the segment informations 
# to make it into a multi-segment GPDB cluster

REMOTEDATADIR=/Users/pivotal/workspace/remote_datadirs

# promote remote master
pg_ctl -D $REMOTEDATADIR/master/ promote

# promote all remote segments
pg_ctl -D $REMOTEDATADIR/primary1/ promote
pg_ctl -D $REMOTEDATADIR/primary2/ promote
pg_ctl -D $REMOTEDATADIR/primary3/ promote

# sleep for some time so that they are fully promoted.
sleep 5s

# get rid of all rows in gp_segment_configuration (to clean broken connections)
PGOPTIONS="-c gp_session_role=utility" psql -p 17432 postgres -c "
    set allow_system_table_mods=true;
    truncate gp_segment_configuration;
    "

# add remote segments (master, segments) to remote gp_segment_configuration table.
PGOPTIONS="-c gp_session_role=utility" psql -p 17432 postgres -c "
    select pg_catalog.gp_add_segment(8::smallint, -1::smallint, 'p', 'p', 'n', 'u', 17432, '$(hostname)', '$(hostname)', '$REMOTEDATADIR/master');
    select pg_catalog.gp_add_segment(5::smallint, 0::smallint, 'p', 'p', 'n', 'u', 27432, '$(hostname)', '$(hostname)', '$REMOTEDATADIR/primary1');
    select pg_catalog.gp_add_segment(6::smallint, 1::smallint, 'p', 'p', 'n', 'u', 27433, '$(hostname)', '$(hostname)', '$REMOTEDATADIR/primary2');
    select pg_catalog.gp_add_segment(7::smallint, 2::smallint, 'p', 'p', 'n', 'u', 27434, '$(hostname)', '$(hostname)', '$REMOTEDATADIR/primary3');
    "

# Make sure PGPORT=master port, MASTER_DATA_DIRECTORY=master directory
export PGPORT=17432
export MASTER_DATA_DIRECTORY=$REMOTEDATADIR/master

# kill the configured GPDB cluster and run gpstart to make it fully functional.
killall postgres
sleep 5s
gpstart -a

# Change 'synchronous_standby_names' parameter to be empty in remote primaries' postgresql.auto.confs
# in order stop remote primaries from attempting replication to a nonexistant mirror.
PGOPTIONS="-c gp_session_role=utility" psql -p 27432 postgres -c "
  ALTER SYSTEM SET synchronous_standby_names = '';
  "
PGOPTIONS="-c gp_session_role=utility" psql -p 27433 postgres -c "
  ALTER SYSTEM SET synchronous_standby_names = '';
  "
PGOPTIONS="-c gp_session_role=utility" psql -p 27434 postgres -c "
  ALTER SYSTEM SET synchronous_standby_names = '';
  "

# reload the synchronous_standby_names non-session_level GUC on greenplum
gpstop -au
