#! /bin/bash

# clean up used directories and sets them up again.

mkdir -p $HOME/workspace/remote_datadirs
rm -rf $HOME/workspace/remote_datadirs/*

rm -rf /tmp/staging
mkdir -p /tmp/staging

rm -rf /tmp/remote_sink
mkdir -p /tmp/remote_sink/master
mkdir -p /tmp/remote_sink/primary1
mkdir -p /tmp/remote_sink/primary2
mkdir -p /tmp/remote_sink/primary3

rm -rf /tmp/remote_datadirs
mkdir -p /tmp/remote_datadirs/master
mkdir -p /tmp/remote_datadirs/primary1
mkdir -p /tmp/remote_datadirs/primary2
mkdir -p /tmp/remote_datadirs/primary3
