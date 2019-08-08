#! /bin/bash

# kill all producer and consumer processes
pkill pg_receivexlog
pkill jk
