#! /bin/sh

kill -0 $1 >/dev/null 2>&1
if [ $? -ne 0 ] ; then
  echo "gone"
  exit 1
fi

timeout 5 ls mnt >/dev/null
if [ $? -ne 0 ] ; then exit 1 ; fi
timeout 5 cat mnt/inner/c >/dev/null
if [ $? -ne 0 ] ; then exit 1 ; fi

exit 0
