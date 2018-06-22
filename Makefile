#
# Makefile for the CS:APP Shell Lab
# 
# Type "make" to build your shell and driver
#
CC = /usr/bin/gcc
CFLAGS = -Wall -g -O2 -Werror
LIBS = -lpthread

WRAPCFLAGS = -Wl,--wrap,fork,--wrap,sigsuspend,--wrap,sigprocmask,--wrap,printf,--wrap,fprintf,--wrap,sprintf,--wrap,snprintf,--wrap,init_job_list,--wrap,kill,--wrap,waitpid,--wrap,get_pid_of_job,--wrap,execve

FILES = sdriver runtrace tsh myspin1 myspin2 myenv \
    myintp myints mytstpp mytstps mysplit mysplitp mycat \
    mysleepnprint

all: $(FILES)

#
# Using link-time interpositioning to introduce non-determinism in the
# order that parent and child execute after invoking fork
#
tsh: tsh.c wrapper.c csapp.c csapp.h sio_printf.c sio_printf.h tsh_helper.c tsh_helper.h
	$(CC) $(CFLAGS) $(WRAPCFLAGS) -o tsh tsh.c wrapper.c csapp.c sio_printf.c tsh_helper.c $(LIBS)

sdriver: sdriver.o
sdriver.o: sdriver.c config.h
runtrace: runtrace.c csapp.c config.h sio_printf.c sio_printf.h csapp.h
	$(CC) $(CFLAGS) -o runtrace runtrace.c csapp.c sio_printf.c $(LIBS)

# Clean up
clean:
	rm -f $(FILES) *.o *~

# Create Hand-in
handin:
	#tar cvf handin.tar tsh.c key.txt
	@echo 'Do not submit a handin.tar file to Autolab. Instead, upload your tsh.c file directly.'
