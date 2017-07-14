.SUFFIXES : .c .o

CC = gcc
# debug level
DEBUG = -g3
#optimization level
OPTIMIZATION = -O0 
# warnning message
WARNING = -Wall
#-Wall

# source 
TXD_CUR=$(CURDIR)
TXD_SRC=$(TXD_CUR)/src
TXD_BIN=$(TXD_CUR)/bin

# TXDRIVER headers
INC = ${CURDIR}/include/

# spdk, dpdk path 
SPDK_ROOT_DIR = /home/son/SKT/spdk
DPDK_DIR = /home/son/SKT/dpdk/dpdk-stable-17.05.1

# spdk library path, name
SPDK_LIBS_DIR = $(SPDK_ROOT_DIR)/build/lib
SPDK_LIBS = -lspdk_nvme -lspdk_util -lspdk_log -lspdk_env_dpdk

# dpdk library path, name
DPDK_LIBS_DIR = $(DPDK_DIR)/build/lib/
DPDK_LIBS = -lrte_eal -lrte_mempool -lrte_ring

# flags
CFLAGS = -I $(DPDK_DIR)/build/include -I $(SPDK_ROOT_DIR)/include -I $(SPDK_ROOT_DIR) -I $(INC)\
		 $(WARNING) $(DEBUG) $(OPTIMIZATION) -pthread -D_GNU_SOURCE 
LDFLAGS = -L $(SPDK_LIBS_DIR) $(SPDK_LIBS) -L $(DPDK_LIBS_DIR) $(DPDK_LIBS) \
		  $(WARNING) -pthread -laio -lrt -ldl 

# source
OBJS_TXDRIVER = $(TXD_SRC)/txdriver_spdk.o $(TXD_SRC)/txdriver_api.o
SRCS_TXDRIVER = $(TXD_SRC)/$(OBJS_TXDRIVER:.o=.c)

TARGET_TXDRIVER = $(TXD_BIN)/modifying
LIB_TXDRIVER = $(TXD_BIN)/txdriver.a

#.PHONY: all $(DIRS-y) clean 

.c.o:
	@echo "Compilingi NV Transaction Driver $< ..."
	@$(CC) $(DEBUG) $(CFLAGS) -c $< -o $@

all: $(TARGET_TXDRIVER) $(LIB_TXDRIVER)

$(TARGET_TXDRIVER) : $(OBJS_TXDRIVER)
	@echo "Start building..."
	@$(CC) -o $(TARGET_TXDRIVER) $(OBJS_TXDRIVER) $(LDFLAGS)
	@echo "Build done."

$(LIB_TXDRIVER) : $(OBJS_TXDRIVER)
	$(AR) rcv $@ $(OBJS_TXDRIVER)

dep : 
	gccmaedep $(INC) $(SRCS_TXDRIVER)

clean:
	@echo "Cleaning TARGET_TXDRIVERs..."
	@rm -rf $(TXD_SRC)/*.o
	@rm -rf $(TXD_BIN)/$(TARGET_TXDRIVER) $(TXD_BIN)/$(LIB_TXDRIVER)
	@echo "Cleaned." 


