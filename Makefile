CC=mpicc
HDF5_DIR=../hdf5
ROOTLESS_DIR=./rootless/
CFLAGS=-g -O0 -Wall #-fPIC
INCLUDES=-I$(HDF5_DIR)/include -I$(ROOTLESS_DIR)
LIBS=-L$(HDF5_DIR)/lib -L$(ROOTLESS_DIR) -lrlo -lhdf5 -lz
SRC= H5VL_rlo.c VotingManager.c VotingPlugin_RLO.c LedgerManager.c ExecutionManager.c metadata_update_helper.c proposal.c util_queue.c
RLO_VOL_PATH=./# or $(YOUR_OWN_RLO_VOL_DIR)
TARGET=libh5rlo.so #TARGET=libh5rlo.so
BIN=testcase_rlo_vol

all: static test #so

#so:
#       $(CC) -shared $(CFLAGS) $(INCLUDES) -L$(HDF5_DIR)/lib -L$(ROOTLESS_DIR)  -lrlo -lhdf5 -lz -o $(TARGET) -fPIC $(SRC) -fPIC

static:
	$(CC) $(CFLAGS) -c $(INCLUDES)  H5VL_rlo.c -o H5VL_rlo.o
	$(CC) $(CFLAGS) -c VotingManager.c -o VotingManager.o
	$(CC) $(CFLAGS) -c $(INCLUDES)  VotingPlugin_RLO.c -o VotingPlugin_RLO.o
	$(CC) $(CFLAGS) -c LedgerManager.c -o LedgerManager.o
	$(CC) $(CFLAGS) -c ExecutionManager.c -o ExecutionManager.o
	$(CC) $(CFLAGS) -c metadata_update_helper.c -o metadata_update_helper.o
	$(CC) $(CFLAGS) -c proposal.c -o proposal.o
	$(CC) $(CFLAGS) -c util_queue.c -o util_queue.o
	ar rcs libh5rlo.a H5VL_rlo.o VotingManager.o VotingPlugin_RLO.o LedgerManager.o ExecutionManager.o metadata_update_helper.o proposal.o util_queue.o #../../rootless/rootless_ops.o

test:
	$(CC)  $(CFLAGS) -c $(INCLUDES)  testcase_rlo_vol.c -o testcase_rlo_vol.o
	$(CC) -g -O0 testcase_rlo_vol.o -L$(HDF5_DIR)/lib -L$(ROOTLESS_DIR) -L$(RLO_VOL_PATH)  $(RLO_VOL_PATH)/libh5rlo.a  -lhdf5 -lrlo -lz -o $(BIN)

data_clean:
	rm *.h5
clean:
	rm *.o *.a *.so $(BIN)
