#! /bin/sh

cd "${0%/*}"

DELAY=5
KILLDELAY=30
while true ; do
  if [ -f fuse8.pid ] ; then
    PID=`cat fuse8.pid`
    if [ "x$PID" != "x" ] ; then
      kill -0 $PID >/dev/null 2>&1
      if [ $? -ne 0 ] ; then
        echo "fuse8 process went away without tidying up pid file"
        rm -f fuse8.pid
      fi
    fi
  fi
  if [ ! -f fuse8.pid ] ; then
    echo "no fuse8 process found, starting"
    nohup ./fuse8 "$@" >/dev/null 2>&1 &
    sleep $DELAY
  fi
  while [ ! -f fuse8.pid ] ; do
    sleep 1
  done
  PID=`cat fuse8.pid`
  while true; do
    ./fuse8-check.sh $PID
    if [ $? -ne 0 ] ; then
      echo "fuse8 check failed, killing and restarting"
      kill -TERM $PID >/dev/null 2>&1
      for i in `seq $KILLDELAY`; do
        kill -0 $PID >/dev/null 2>&1
        if [ $? -ne 0 ] ; then
          break
        fi
        sleep 1
      done
      kill -KILL $PID >/dev/null 2>&1
      break
    fi
    sleep $DELAY
  done
  echo "gone"
done
