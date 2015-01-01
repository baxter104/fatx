#!/bin/bash

SIZE=300
WAIT=3
TIMEOUT=120

DSK=disk.fatx
CMDFUSE="./fusefatx -d $DSK mnt/"

prepare() {
	echo Prepare context
	[ -e fusefatx ] || ln -sf fatx fusefatx
	[ -e mkfs.fatx ] || ln -sf fatx mkfs.fatx
	[ -e unrm.fatx ] || ln -sf fatx unrm.fatx
	[ -e fsck.fatx ] || ln -sf fatx fsck.fatx
	[ -e label.fatx ] || ln -sf fatx label.fatx
	[ -d mnt ] || mkdir mnt
	[ -e $DSK ] || dd if=<(yes $'\xFF' | tr -d "\n") of=$DSK bs=$((1024*1024)) count=$SIZE iflag=fullblock
#	[ -e $DSK ] || dd if=/dev/urandom of=$DSK bs=$((1024*1024)) count=600 iflag=fullblock
	rm -rf mnt/* || (fusermount -u mnt && rm -rf mnt/*)
}
remove() {
	rm -rf mnt
	rm -f fusefatx
	rm -f mkfs.fatx
	rm -f unrm.fatx
	rm -f fsck.fatx
	rm -f label.fatx
}

prefuse() {
	$CMDFUSE $1 2>&1 &
	FUSE=$!
	count=0
	while ! `df mnt | tail -n1 | cut -f 1 -d\  | grep -q fatx`; do
		sleep 1
		let count++
		if [ "$1" == "" -a $((count)) == $TIMEOUT ]; then
			echo Failed to mount disk
			kilfuse
			exit 1
		fi
	done
}
remfuse() {
	sleep $WAIT
	fusermount -u mnt
	count=0
	while kill -0 $FUSE 2>/dev/null; do
		sleep 1
		let count++
		if [ $((count)) == $TIMEOUT ]; then
			echo Failed to unmount disk
			kilfuse
			exit 1
		fi
	done
	FUSE=
}
kilfuse() {
	kill -9 $FUSE 2>/dev/null
	FUSE=
	fusermount -u mnt
}

mkfs1() {
	echo Mkfs: make disk: 
	./mkfs.fatx -y $DSK 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		exit 1
	fi
}

fuse1() {
	echo Fuse: simple file creation: 
	prefuse
	cp fatx mnt
	cmp -b fatx mnt/fatx
	if [ $? != 0 ]; then
		echo "### Test KO"
		kilfuse
		exit 1
	fi
	cp Makefile mnt/fatx
	cmp -b Makefile mnt/fatx
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		kilfuse
		exit 1
	fi
	remfuse
}
fuse2() {
	echo Fuse: moving file: 
	prefuse
	mkdir mnt/fuse2
	mkdir mnt/fuse2/dir1
	mkdir mnt/fuse2/dir2
	mv -f mnt/fatx mnt/fatx.bis
	mv -f mnt/fatx.bis mnt/fuse2/fatx
	mv -f mnt/fuse2/fatx mnt/fuse2/dir1/
	mv -f mnt/fuse2/dir1 mnt/fuse2/dir3
	mv -f mnt/fuse2/dir3 mnt/fuse2/dir2/
	ls mnt/fuse2/dir2/dir3/fatx >/dev/null 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		kilfuse
		exit 1
	fi
	remfuse
}
fuse3() {
	echo Fuse: removing file: 
	prefuse
	mkdir mnt/fuse3
	cp fatx mnt/fuse3
	rm mnt/fuse3/fatx
	rmdir mnt/fuse3
	ls mnt/fuse3 >/dev/null 2>&1
	if [ $? != 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		kilfuse
		exit 1
	fi
	remfuse
}
fuse4() {
	echo Fuse: multiple file access: 
	prefuse
	mkdir mnt/fuse4
	nmax=5
	for ((n = 1; n <= $nmax; n++)); do
		cp Makefile mnt/fuse4/m$n
	done
	exec 3<>mnt/fuse4/m1
	exec 4<>mnt/fuse4/m2
	exec 5<>mnt/fuse4/m3
	exec 6<mnt/fuse4/m4
	exec 7>mnt/fuse4/m5
	exec 7>&-
	exec 6>&-
	exec 5>&-
	exec 4>&-
	exec 3>&-
	echo "*** Test OK"
	remfuse
}
fuse5() {
	echo Fuse: concurrent access: 
	prefuse
	./fsck.fatx $DSK 2>&1
	if [ $? != 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		kilfuse
		exit 1
	fi
	remfuse
}
fuse6() {
	echo Fuse: directory copies:
	prefuse
	mkdir mnt/test
	cp fatx mnt/test/
	mkdir mnt/fuse6
	cp -r mnt/test mnt/fuse6
	echo "*** Test OK"
	remfuse
}
fuse7() {
	echo Fuse: simultaneous copies:
	prefuse
	mkdir mnt/fuse7
	tasks=
	nmax=50
	for ((n = 1; n <= $nmax; n++)); do
		dd if=/dev/urandom of=mnt/fuse7/r$n bs=$((2 * 1024 * 1024 + 256)) count=1 >/dev/null 2>&1
		if [ `du -b mnt/fuse7/r$n | cut -f 1` != $((2 * 1024 * 1024 + 256)) ]; then
			echo "### Test KO", invalid file size
			kilfuse
			exit 1
		fi
	done
	sleep $WAIT
	for ((n = 1; n <= $nmax; n++)); do
		cp mnt/fuse7/r$n mnt/fuse7/w$n &
		tasks+=$!" "
		sleep 0.1
	done
	tasks+=$!
	nbtasks=${#tasks[*]}
	error=1
	ended=
	for ((n = 1; $TIMEOUT == 0 || n <= $TIMEOUT; n++)); do
		sleep 1
		for pid in ${tasks}; do
			kill -0 $pid 2>/dev/null
			if (! kill -0 $pid 2>/dev/null) || !(echo $ended | grep $pid); then
				ended+=$pid" "
			fi
		done
		if [ ${#ended[*]} == $nbtasks ]; then
			error=0
			break;
		fi
	done
	for ((n = 1; n <= $nmax; n++)); do
		sleep 1
		cmp -b mnt/fuse7/r$n mnt/fuse7/w$n || {
			echo "### Test KO", files are different
			kilfuse
			exit 1
		}
	done
	if [ $error != 0 ]; then
		echo "### Test KO", timeout reached
		kilfuse
		exit 1
	fi
	echo "*** Test OK"
	remfuse
}
fuse8() {
	echo Fuse: directory max entries:
	prefuse
	mkdir mnt/fuse8
	maxent=512
	for ((i = 0; i < $maxent; i++)); do
		touch mnt/fuse8/ent$i
	done
	remfuse
	prefuse
	if [ `ls mnt/fuse8/ent* | wc -w` != $maxent ]; then
		echo "### Test KO"
		kilfuse
		exit 1
	else
		echo "*** Test OK"
	fi
	remfuse
}
fuse9() {
	echo Fuse: recover mode:
	prefuse
	mkdir mnt/fuse9
	cp fatx mnt/fuse9/tbff0
	rm mnt/fuse9/tbff0
	remfuse
	prefuse -r
	test -e mnt/fuse9/tbff0 && cmp -b fatx mnt/fuse9/tbff0
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO", file not found or files are different
		kilfuse
		exit 1
	fi
	remfuse
}
fuse10() {
	echo Fuse: FAT stress:
	prefuse
	mkdir mnt/fuse10
	avail1=$(df -k mnt | tail -1 | tr -s ' ' | cut -f3 -d' ')
	nmax=100
	for ((n = 1; n <= $nmax; n++)); do
		size=$[($RANDOM % 1000) + 1]
		dd if=/dev/urandom of=mnt/fuse10/ent$n bs=$((size * 1024 + 256)) count=1 >/dev/null 2>&1
		if [ `du -b mnt/fuse10/ent$n | cut -f 1` != $((size * 1024 + 256)) ]; then
			echo "### Test KO", invalid file size
			kilfuse
			exit 1
		fi
	done
	avail2=$(df -k mnt | tail -1 | tr -s ' ' | cut -f3 -d' ')
	avail3=$(du -k mnt/fuse10 | tail -1 | cut -f1)
	if [ $avail3 == $[$avail2 - $avail1] ]; then
		echo "*** Test OK", Used = $avail3, Occupped = $[$avail2 - $avail1]
	else
		echo "### Test KO", Used = $avail3, Occupped = $[$avail2 - $avail1]
		kilfuse
		exit 1
	fi
	remfuse
}
fuse11() {
	echo Fuse: directory entries stress:
	prefuse
	mkdir mnt/fuse11
	maxent=1024
	tasks=
	for ((i = 0; i < $maxent; i++)); do
		echo "TEST" >mnt/fuse11/ent$i &
		tasks+=$!" "
	done
	for job in $tasks; do
		wait $job
	done
	remfuse
	prefuse
	if [ `ls mnt/fuse11/ent* | wc -w` != $maxent ]; then
		echo "### Test KO"
		kilfuse
		exit 1
	else
		echo "*** Test OK"
	fi
	remfuse
}
fuse12() {
	echo Fuse: simultaneous creations:
	prefuse
	mkdir mnt/fuse12
	maxent=100
	tasks=
	for ((i = 0; i < $maxent; i++)); do
		echo "TEST" >mnt/fuse12/ent &
		tasks+=$!" "
	done
	for job in $tasks; do
		wait $job
	done
	remfuse
	prefuse
	if [ `ls mnt/fuse12/ent* | wc -w` != 1 ]; then
		echo "### Test KO"
		kilfuse
		exit 1
	else
		echo "*** Test OK"
	fi
	remfuse
}
fuse99() {
	echo Fuse: check statfs:
	prefuse
	rm -rf mnt/*
	MIN=$(($(df -k mnt | tail -1 | tr -s ' ' | cut -f3 -d' ') / 2))
	echo TEST >mnt/fuse99
	[ $(($MIN * 4)) -ge $(df -k mnt | tail -1 | tr -s ' ' | cut -f3 -d' ') ]
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO, found "$(df -k mnt | tail -1 | tr -s ' ' | cut -f3 -d' ')" expected < "$(($MIN * 4))
		kilfuse
		exit 1
	fi
	remfuse
}

fsck1() {
	echo Fsck: sanity check:
	./fsck.fatx -nv $DSK 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		exit 1
	fi
}

labl1() {
	echo Label: check default name: 
	./label.fatx $DSK 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		exit 1
	fi
}
labl2() {
	echo Label: check noname: 
	prefuse
	rm mnt/name.txt
	remfuse
	./label.fatx $DSK 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		exit 1
	fi
}
labl3() {
	echo Label: set label 
	./label.fatx $DSK $DSK 2>&1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO"
		exit 1
	fi
}

unrm1() {
	echo Unrm: remote recovery: 
	prefuse
	mkdir mnt/unrm1
	cp fatx mnt/unrm1/tbff1
	rm mnt/unrm1/tbff1
	remfuse
	./unrm.fatx -y $DSK 2>&1
	if [ $? != 0 ]; then
		echo "### Test KO", unrm failed
		exit 1
	fi
	prefuse
	test -e mnt/unrm1/tbff1 && cmp -b fatx mnt/unrm1/tbff1
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO", file not recovered or files are different
		kilfuse
		exit 1
	fi
	remfuse
}
unrm2() {
	echo Unrm: remote dir. recovery:
	prefuse
	mkdir mnt/unrm2
	cp fatx mnt/unrm2/tbff2
	rm mnt/unrm2/tbff2
	rmdir mnt/unrm2
	remfuse
	./unrm.fatx -y $DSK 2>&1
	if [ $? != 0 ]; then
		echo "### Test KO", unrm failed
		exit 1
	fi
	prefuse
	test -d mnt/unrm2 && test -e mnt/unrm2/tbff2 && cmp -b fatx mnt/unrm2/tbff2
	if [ $? == 0 ]; then
		echo "*** Test OK"
	else
		echo "### Test KO", file not recovered or files are different
		kilfuse
		exit 1
	fi
	remfuse
}
unrm3() {
	echo Unrm: local recovery:
	[ -e ./tbff3 ] && rm -f ./tbff3
	prefuse
	mkdir mnt/unrm3
	cp fatx mnt/unrm3/tbff3
	rm mnt/unrm3/tbff3
	remfuse
	./unrm.fatx -ly $DSK 2>&1
	if [ $? != 0 ]; then
		echo "### Test KO", unrm failed
		exit 1
	fi
	test -e tbff3 && cmp -b fatx tbff3
	if [ $? == 0 ]; then
		echo "*** Test OK"
		rm tbff3
	else
		echo "### Test KO", file not recovered or files are different
		exit 1
	fi
}
unrm4() {
	echo Unrm: lost chain recovery: 
	./fatx --as mkfs disk.fatx -vy
	dd if=/dev/urandom of=tbff4 bs=$((1024 * 1024 + 256)) count=1 >/dev/null 2>&1
	./fatx --as label disk.fatx -l XBOX -v --do "\
		mkdir,	/unrm4; \
		lsfat,	/; \
		lsfat,	/name.txt; \
		lsfat,	/unrm4; \
		rcp,	tbff4, /unrm4/tbff4.bak; \
		lsfat,	/unrm4/tbff4.bak; \
		rm,		/unrm4/tbff4.bak; \
		rmdir,	/unrm4; \
		mklost,	30:32; \
		mklost,	40:42; \
		mklost,	67:68; \
		mklost,	100:110; \
		rmfat,	31; \
	"
	./fatx --as fsck disk.fatx -vn
	./fatx --as unrm disk.fatx -vy
	./fatx --as fsck disk.fatx -vn
	./fatx --as label disk.fatx -v --do "\
		lsfat,	/unrm4/tbff4.bak; \
		lcp,	/unrm4/tbff4.bak, tbff4.bak; \
	"
	cmp -b tbff4 tbff4.bak
	if [ $? == 0 ]; then
		echo "*** Test OK"
		rm tbff4 tbff4.bak
	else
		echo "### Test KO", file not recovered or files are different
		exit 1
	fi
}

close() {
	remove
	rm $DSK
}

scenario() {
	df -h mnt
	cp -r debian mnt/
	rm -rf mnt/debian
	cp fatx mnt
	mkdir mnt/fuse2
	mkdir mnt/fuse2/dir1
	mkdir mnt/fuse2/dir2
	mv -f mnt/fatx mnt/fatx.bis
	mv -f mnt/fatx.bis mnt/fuse2/fatx
	mv -f mnt/fuse2/fatx mnt/fuse2/dir1/
	mv -f mnt/fuse2/dir1 mnt/fuse2/dir3
	mv -f mnt/fuse2/dir3 mnt/fuse2/dir2/
	mkdir mnt/fuse7
	nmax=10
	for ((n = 1; n <= $nmax; n++)); do
		dd if=/dev/urandom of=mnt/fuse7/r$n bs=$((1024*1024)) count=1 >/dev/null 2>&1
	done
	for ((n = 1; n < $nmax; n++)); do
		cp mnt/fuse7/r$n mnt/fuse7/w$n &
	done
	wait
	df -h mnt
	sleep $WAIT
	fusermount -u mnt
}

tests=(
	close
	mkfs1
	fuse1
	fuse2
	fuse3
	fuse4
	fuse5
	fuse6
	fuse7
	fuse8
	fuse9
	fuse10
	fuse11
	fuse12
	fsck1
	labl1
	labl2
	labl3
	unrm1
	unrm2
	unrm3
	fuse99
	fsck1
	unrm4
)
testn=`basename $0`

case $testn in
test.sh)
	for ((i = 0; i < ${#tests[*]}; i++)); do
		[ -e test$i ] || ln -s test.sh test$i
	done
	[ -e analyse.sh ] || ln -s test.sh analyse.sh
	[ -e profile.sh ] || ln -s test.sh profile.sh
	;;
analyse.sh)
	rm [0-9A-F]*.log >/dev/null 2>&1
	IFS=$'\n'
	sed -e 's/\([^ ]\{2\} {[0-9A-F]\{8\}}\)/\n\1/g' >/tmp/$$$$
	for pid in `cat /tmp/$$$$ | cut -f 2 -d\{ | cut -f 1 -d\} | grep ^[0-9A-F]*$ | sort | uniq`; do
		grep $pid /tmp/$$$$ >$pid.log
	done
	rm /tmp/$$$$
	;;
profile.sh)
	#PRE=--gen-suppressions=all
	./fatx --as mkfs -y $DSK
	valgrind $PRE --tool=memcheck --log-file=./leaks1.log $CMDFUSE
	scenario
	./fatx --as mkfs -y $DSK
	valgrind $PRE --tool=helgrind --log-file=./locks1.log $CMDFUSE
	scenario
	./fatx --as mkfs -y $DSK
	valgrind $PRE --tool=drd --log-file=./locks2.log $CMDFUSE
	scenario
	;;
*)
	testn=${testn/test/}
	testr=${tests[$testn]}
	if [ "$testr" != "unrm4" ]; then
		trap kilfuse SIGINT
	fi
	prepare
	$testr
	;;
esac
