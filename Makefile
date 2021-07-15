#
# Copyright (c) 2018-2021 Seagate Technology LLC and/or its Affiliates
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# For any questions about this software or licensing,
# please email opensource@seagate.com or cortx-questions@seagate.com.
#
# Original author:  Ganesan Umanesan <ganesan.umanesan@seagate.com>
# Original creation date: 10-Jan-2017
#

#sudo
SUDO = sudo
#or not
undefine SUDO

#files
FILE1 = './file1'
FILE2 = './file2'
FILE3 = './file3'

#executables
C0CP = c0cp
C0CT = c0cat
C0RM = c0rm
FGEN = fgen

RCDIR = $(HOME)/.c0appz
APPASSIGN = sage-user-application-assignment

#isc executables and library
LIBISC = libdemo.so
ISC_REG = c0isc_reg
ISC_INVK = c0isc_demo

#archive/node names
TARF = m0trace_$(shell date +%Y%m%d-%H%M%S).tar.bz2
TARN = $(shell ls -la m0trace.* &> /dev/null | wc -l)
NODE = $(shell eval uname -n)

#compiler/linker options
LFLAGS += -lm -lpthread -lrt -lyaml -luuid -lmotr
CFLAGS += -I. -I/usr/include/motr
CFLAGS += -D_REENTRANT -D_GNU_SOURCE -DM0_INTERNAL='' -DM0_EXTERN=extern
CFLAGS += -fno-common -Wall -Werror -Wno-attributes -fno-strict-aliasing
CFLAGS += -fno-omit-frame-pointer -g -O2 -Wno-unused-but-set-variable
CFLAGS += -rdynamic
# generate .d dependencies automatically
CFLAGS += -MP -MD
IQUOTEDIR := /usr/include/motr
M0GCCXML2XCODE := m0gccxml2xcode
ifneq ($(M0_SRC_DIR),)
LFLAGS += -L$(M0_SRC_DIR)/motr/.libs -Wl,-rpath=$(M0_SRC_DIR)/motr/.libs
CFLAGS += -I$(M0_SRC_DIR) -I$(M0_SRC_DIR)/extra-libs/galois/include
IQUOTEDIR := $(M0_SRC_DIR)
M0GCCXML2XCODE := $(M0_SRC_DIR)/xcode/m0gccxml2xcode
endif

SRC = perf.o buffer.o qos.o c0appz.o
SRC_ALL = $(SRC) c0cp.o c0cat.o c0rm.o fgen.o \
		c0isc_register.o c0isc_demo.o isc_libdemo.o

all: $(C0CP) $(C0CT) $(C0RM) $(FGEN) isc-all
.PHONY: all

-include $(SRC_ALL:.o=.d)

%.o: %.c
	gcc -c $(CFLAGS) -o $@ $<

$(C0CP): c0cp.c $(SRC)
	gcc $(CFLAGS) $(LFLAGS) $(SRC) -o $@ $<

$(C0CT): c0cat.c $(SRC)
	gcc $(CFLAGS) $(LFLAGS) $(SRC) -o $@ $<

$(C0RM): c0rm.c $(SRC)
	gcc $(CFLAGS) $(LFLAGS) $(SRC) -o $@ $<

$(FGEN):
	gcc -lssl -lcrypto fgen.c -o $(FGEN)

#dd block size, count and filesize
#dd can have any block size and a random
#number of blocks, making the file size
#random
DDZ := 1032
CNT := $(shell expr 100 + $$RANDOM % 1000)
#CNT := $(shell expr 1024 \* 16)
#CNT := 1024
FSZ := $(shell expr $(DDZ) \* $(CNT))

#c0cp block size
#valid block sizes are: 4KB ~ 32MB
#4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288,
#1048576, 2097152, 4194304, 8388608, 16777216, 33554432
BSZ := 64

test: $(C0CP) $(C0CT) $(C0RM) $(FGEN)
	$(eval FID1 := $(shell eval ./$(FGEN)))
	$(eval FID2 := $(shell eval ./$(FGEN)))

	$(SUDO) dd if=/dev/urandom of=$(FILE1) bs=$(DDZ) count=$(CNT)
	@echo "#####"
	@ls -lh $(FILE1)
	$(SUDO) ./$(C0CP) $(FID1) $(FILE1) $(BSZ) -p
	@echo "#####"
	@ls -la $(FILE1)
	$(SUDO) ./$(C0CT) $(FID1) $(FILE2) $(BSZ) $(FSZ) -p
	@ls -la $(FILE2)
	@echo "#####"
	@ls -la $(FILE1)
	$(SUDO) ./$(C0CP) $(FID2) $(FILE1) $(BSZ) -a 8 -p
	@echo "#####"
	$(SUDO) ./$(C0CT) $(FID2) $(FILE3) $(BSZ) $(FSZ) -p
	@ls -ls $(FILE3)
	@echo "#####"
	cmp $(FILE1) $(FILE2) || echo "ERROR: Test Failed !!"
	@echo "#####"
	cmp $(FILE1) $(FILE3) || echo "ERROR: Async Test Failed !!"
	@echo "#####"
	$(SUDO) ./$(C0RM) $(FID1) -y
	$(SUDO) ./$(C0RM) $(FID2) -y

#yaml
#bundle trace files for shipment
yaml:
	@ls m0trace.* &> /dev/null || (echo "No traces!" && exit 1)
	@for file in m0trace.*; do					\
		echo $$file;							\
	ls -lh $$file;								\
		m0trace -Y -i $$file -o $$file.yml;		\
		ls -lh $$file.yml;						\
	done
	tar -jcvf $(TARF) m0trace.*.yml
	tar -jtvf $(TARF)
	rm -f m0trace.*.yml
	ls -lh $(TARF)

vmrcf:
	mkdir -p $(RCDIR)/${C0CP}rc
	mkdir -p $(RCDIR)/${C0CT}rc
	mkdir -p $(RCDIR)/${C0RM}rc
	mkdir -p $(RCDIR)/$(ISC_REG)rc
	mkdir -p $(RCDIR)/$(ISC_INVK)rc
	./scripts/c0appzrcgen > $(RCDIR)/$(C0CP)rc/$(NODE)
	./scripts/c0appzrcgen > $(RCDIR)/$(C0CT)rc/$(NODE)
	./scripts/c0appzrcgen > $(RCDIR)/$(C0RM)rc/$(NODE)
	./scripts/c0appzrcgen > $(RCDIR)/$(ISC_REG)rc/$(NODE)
	./scripts/c0appzrcgen > $(RCDIR)/$(ISC_INVK)rc/$(NODE)

sagercf:
	mkdir -p $(RCDIR)/${C0CP}rc
	mkdir -p $(RCDIR)/${C0CT}rc
	mkdir -p $(RCDIR)/${C0RM}rc
	mkdir -p $(RCDIR)/$(ISC_REG)rc
	mkdir -p $(RCDIR)/$(ISC_INVK)rc
#	$(APPASSIGN) ganesan $(C0CP) 172.18.1.${c} > $(RCDIR)/$(C0CP)rc/client-${c}
#	$(APPASSIGN) ganesan $(C0CT) 172.18.1.${c} > $(RCDIR)/$(C0CT)rc/client-${c}
#	$(APPASSIGN) ganesan $(C0RM) 172.18.1.${c} > $(RCDIR)/$(C0RM)rc/client-${c}
#	$(APPASSIGN) ganesan $(C0CP) 172.18.1.${c} > $(RCDIR)/$(ISC_REG)rc/client-${c}
#	$(APPASSIGN) ganesan $(C0CT) 172.18.1.${c} > $(RCDIR)/$(ISC_INVK)rc/client-${c}
	HOSTNAME=$(NODE) ./scripts/motraddr.sh > out-$(HOSTNAME).txt
	@echo "#####"
	cat ./out-$(HOSTNAME).txt
	@echo "#####"
	cp out-$(HOSTNAME).txt $(RCDIR)/$(C0CP)rc/$(NODE)
	cp out-$(HOSTNAME).txt $(RCDIR)/$(C0CT)rc/$(NODE)
	cp out-$(HOSTNAME).txt $(RCDIR)/$(C0RM)rc/$(NODE)
	cp out-$(HOSTNAME).txt $(RCDIR)/$(ISC_REG)rc/$(NODE)
	cp out-$(HOSTNAME).txt $(RCDIR)/$(ISC_INVK)rc/$(NODE)
	rm -rf out-$(HOSTNAME).txt
	
clean: isc-clean
	rm -f $(C0CP) $(C0CT) $(C0RM) m0trace.*
	rm -f $(FILE1) $(FILE2) $(FILE3)
	rm -f $(FGEN)
	rm -f sfilet-* snodet-* fidout-*
	rm -f upFile dwFile
	rm -f *.o *.d *.d.*

m0t1fs:
	touch /mnt/m0t1fs/0:3000
	setfattr -n lid -v 8 /mnt/m0t1fs/0:3000
	dd if=/dev/zero of=/mnt/m0t1fs/0:3000 bs=4M count=10
	ls -lh /mnt/m0t1fs/0:3000
	rm -rf /mnt/m0t1fs/0:3000

bigtest:
	make
	make fgen
	touch ./.fgenrc
	./scripts/single_node_test 4096 1024*1   1
	./scripts/single_node_test 4096 1024*2   1
	./scripts/single_node_test 4096 1024*4   1
	./scripts/single_node_test 4096 1024*8   1
	./scripts/single_node_test 4096 1024*16  1
	./scripts/single_node_test 4096 1024*32  1
	./scripts/single_node_test 4096 1024*64  1
	./scripts/single_node_test 4096 1024*128 1
	./scripts/single_node_test 4096 1024*256 1
	./scripts/single_node_test 4096 1024*512 1

#
#MPI Appz
#

MPIX = mpix

$(MPIX): $(SRC) mpiapp.c
	mpicc $(SRC) mpiapp.c -I/usr/include/motr $(CFLAGS) $(LFLAGS) -o $(MPIX)

mpi-clean:
	rm -f *.o *.d *.d.*
	rm -f m0trace.*
	rm -f $(MPIX)

mpi-sagercf:
	mkdir -p $(RCDIR)/${MPIX}rc
	HOSTNAME=$(NODE) ./scripts/motraddr.sh 2 > out-$(HOSTNAME).txt
	@echo "#####"
	cat ./out-$(HOSTNAME).txt
	@echo "#####"
	cp out-$(HOSTNAME).txt $(RCDIR)/$(MPIX)rc/$(NODE)
	rm -rf out-$(HOSTNAME).txt

mpi-test:
	mpirun -hosts $(NODE) -np 2 ./$(MPIX)

#
#ISC Demo
#Sage Function Shipping
#

$(ISC_REG): c0isc_register.o $(SRC)
	gcc $(LFLAGS) $(SRC) -o $@ $<

$(LIBISC): isc_libdemo.c isc/libdemo_xc.c
	gcc $(CFLAGS) -fpic -shared -o $@ isc/libdemo_xc.c $<

$(ISC_INVK): c0isc_demo.o isc/libdemo_xc.o $(SRC)
	gcc $(LFLAGS) $(SRC) isc/libdemo_xc.o -o $@ $<

c0isc_demo.c: isc/libdemo_xc.h

ifneq ($(shell which castxml),)
CXXXML := castxml
CXXXML_FLAGS := --castxml-gccxml
CXXXML_OUTPUT := -o # keep trailing blank-space after -o
CXXXML2XC_FLAGS := --castxml
else
CXXXML := gccxml
# set maximum supported gcc version by gccxml, currently it's 4.2
CXXXML_FLAGS := -DGCC_VERSION=4002 -DENABLE_GCCXML
CXXXML_OUTPUT := -fxml=
CXXXML2XC_FLAGS :=
endif

CXXXML_UNSUPPORTED_CFLAGS := -Wno-unused-but-set-variable -Werror -Wno-trampolines -rdynamic --coverage -pipe -Wp,-D_FORTIFY_SOURCE=2 --param=ssp-buffer-size=4 -grecord-gcc-switches -fstack-protector-strong -fstack-clash-protection -MD -MP
CXXXML_CFLAGS := $(filter-out $(CXXXML_UNSUPPORTED_CFLAGS), $(CFLAGS))

%_xc.h %_xc.c: %.h
	$(CXXXML) $(CXXXML_FLAGS) $(CXXXML_CFLAGS) $(CXXXML_OUTPUT)$(<:.h=.gccxml) $<
	$(M0GCCXML2XCODE) $(CXXXML2XC_FLAGS) -i $(<:.h=.gccxml)

isc-all: $(ISC_REG) $(ISC_INVK) $(LIBISC)
isc-clean:
	rm -f $(ISC_REG) $(ISC_INVK) $(LIBISC) isc/{*_xc.*,*.gccxml}
