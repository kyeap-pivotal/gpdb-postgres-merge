#! /bin/bash

# Kill the original cluster.
# Maser on 15432, primaries on 25432, 25433, 25434.
# Standby master on 16432, primary mirrors on 25435, 25436, 25437.

# standby
kill -9 $(lsof -t -i:16432)
# primary mirrors
kill -9 $(lsof -t -i:25435)
kill -9 $(lsof -t -i:25436)
kill -9 $(lsof -t -i:25437)
# master
kill -9 $(lsof -t -i:15432)
# primaries
kill -9 $(lsof -t -i:25432)
kill -9 $(lsof -t -i:25433)
kill -9 $(lsof -t -i:25434)
