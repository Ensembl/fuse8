#! /bin/sh

cd "${0%/*}"


PIDFILE=/var/local/log/fuse8/fuse8.pid
DELAY=5
KILLDELAY=30
while true ; do
  if [ -f $PIDFILE ] ; then
    PID=`cat $PIDFILE`
    if [ "x$PID" != "x" ] ; then
      kill -0 $PID >/dev/null 2>&1
      if [ $? -ne 0 ] ; then
        echo "fuse8 process went away without tidying up pid file"
        rm -f $PIDFILE
      fi
    fi
  fi
  if [ ! -f $PIDFILE ] ; then
    echo "no fuse8 process found, starting"
    nohup ./fuse8 "$@" >/dev/null 2>&1 &
    sleep $DELAY
  fi
  while [ ! -f $PIDFILE ] ; do
    sleep 1
  done
  PID=`cat $PIDFILE`
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
