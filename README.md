Shell
===
A simple shell in C built for an OS class that supports pipes and basic history.

## Test Run
```
~/Desktop/shell$ ./w4118_sh
$/bin/ls
demo_run.txt  Makefile    README.md  shell.c  shell.h  tester.py    w4118_sh  written.txt
$/bin/cat Makefile | /usr/bin/wc
     26      53     487
$/bin/cat Makefile | /usr/bin/wc -l | /usr/bin/wc -l
1
$/usr/bin/uptime
 20:31:39 up 9 min,  2 users,  load average: 0.00, 0.04, 0.05
$/bin/pwd
/home/w4118/Desktop/shell
$cd /tmp
$/bin/pwd
/tmp
$history
0 /bin/ls
1 /bin/cat Makefile | /usr/bin/wc
2 /bin/cat Makefile | /usr/bin/wc -l | /usr/bin/wc -l
3 /usr/bin/uptime
4 /bin/pwd
5 cd /tmp
6 /bin/pwd
$history 3
 20:35:08 up 13 min,  2 users,  load average: 0.00, 0.02, 0.05
$history c
$history
$exit
```

## Memory Leaks check

```
~/Desktop/shell$ make valgrind
gcc -g -o w4118_sh shell.c -Wall -W
valgrind --leak-check=full --show-leak-kinds=all ./w4118_sh
==3074== Memcheck, a memory error detector
==3074== Copyright (C) 2002-2013, and GNU GPL'd, by Julian Seward et al.
==3074== Using Valgrind-3.10.0 and LibVEX; rerun with -h for copyright info
==3074== Command: ./w4118_sh
==3074==
$/bin/ls
demo_run.txt  Makefile    README.md  shell.c  shell.h  tester.py    w4118_sh  written.txt
$/bin/ls | /usr/bin/wc -l
8
$history
0 /bin/ls
1 /bin/ls | /usr/bin/wc -l
$history -c
$exit
==3074==
==3074== HEAP SUMMARY:
==3074==     in use at exit: 0 bytes in 0 blocks
==3074==   total heap usage: 21 allocs, 21 frees, 109,622 bytes allocated
==3074==
==3074== All heap blocks were freed -- no leaks are possible
==3074==
==3074== For counts of detected and suppressed errors, rerun with: -v
==3074== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```
