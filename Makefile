obj-m := sgdma.o
sgdma-objs := usgproc.o usgdma.o 

USERAPP := test

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

test:mmaptest.c
	gcc -o test mmaptest.c
	