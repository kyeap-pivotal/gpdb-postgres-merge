#! /bin/bash

# bootstraps the remote cluster

REMOTEDATADIR=$HOME/workspace/remote_datadirs

# Make sure there is already a default demo cluster. (i.e. make sure you have run make create-demo-cluster on a gpdb src beforehand.)

# Set checkpoint_timeout GUC to 1 hour.
gpconfig -c checkpoint_timeout -v 3600 --skipvalidation

# Make sure REMOTEDATATDIR is empty
rm -rf $REMOTEDATADIR

# Run pg_basebackup on source cluster to create remote cluster.
mkdir -p $REMOTEDATADIR
pg_basebackup --xlog-method=stream -R -c fast -D $REMOTEDATADIR/primary1 -h "$(hostname)" -p 25432 --target-gp-dbid 5
pg_basebackup --xlog-method=stream -R -c fast -D $REMOTEDATADIR/primary2 -h "$(hostname)" -p 25433 --target-gp-dbid 6
pg_basebackup --xlog-method=stream -R -c fast -D $REMOTEDATADIR/primary3 -h "$(hostname)" -p 25434 --target-gp-dbid 7
pg_basebackup --xlog-method=stream -R -c fast -D $REMOTEDATADIR/master -h "$(hostname)" -p 15432 --target-gp-dbid 8

# Change all remote recovery.confs have primaryconninfo as blank.
for dir in primary1 primary2 primary3 master; do
  sed -i'' -e "s/primary_conninfo.*/primary_conninfo = ''/" $REMOTEDATADIR/$dir/recovery.conf
done

# Change all remote segments' ports to be different from the original segments' ports.
# Let remote master port = 17432, remote primary ports = 27432, 27433, 27434
sed -i'' -e "s/port=15432/port=17432/" $REMOTEDATADIR/master/postgresql.conf
for dir in primary1 primary2 primary3; do
  sed -i'' -e "s/port=25/port=27/" $REMOTEDATADIR/$dir/postgresql.conf
done

# start remote master (demo, will be on already) (on background) (system will start in master-only utility mode)
$GPHOME/bin/postgres -D $REMOTEDATADIR/master/ -p 17432 -c gp_role=utility &

# start remote primaries (demo, will be on already) (on background)
$GPHOME/bin/postgres -D $REMOTEDATADIR/primary1/ -i -p 27432 &
$GPHOME/bin/postgres -D $REMOTEDATADIR/primary2/ -i -p 27433 &
$GPHOME/bin/postgres -D $REMOTEDATADIR/primary3/ -i -p 27434 &

