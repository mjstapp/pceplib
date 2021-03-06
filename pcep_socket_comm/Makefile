AR= ar
ARFLAGS = rc
CC= gcc
CFLAGS += -I$(INC_DIR) -I$(PCEP_UTILS_INC_DIR) -Wall -Werror -fPIC
RANLIB = ranlib

INC_DIR = include
OBJ_DIR = ./obj
SRC_DIR = ./src
BUILD_DIR = ../build
PCEP_UTILS_INC_DIR = ../pcep_utils/include
TEST_DIR = ./test
TEST_LIB_DIRS = -L$(BUILD_DIR) -L/usr/local/lib
TEST_LIBS = -l$(LIB_NAME) -lpcep_utils -lcunit -lpthread
VALGRIND=G_SLICE=always-malloc G_DEBUG=gc-friendly valgrind -v --tool=memcheck --leak-check=full --num-callers=40 --error-exitcode=1

LIB_NAME = pcep_socket_comm
LIB = $(BUILD_DIR)/lib$(LIB_NAME).a
# The socket_comm_mock lib is built as a separate library
LIB_MOCK = $(BUILD_DIR)/libpcep_socket_comm_mock.a
TEST_BIN = $(BUILD_DIR)/pcep_socket_comm_tests

_DEPS = *.h
DEPS = $(patsubst %,$(INC_DIR)/%,$(_DEPS)) $(patsubst %,$(SRC_DIR)/%,$(_DEPS))
EXTERNAL_DEPS = $(patsubst %,$(PCEP_UTILS_INC_DIR)/%,$(_DEPS))

_OBJ = pcep_socket_comm_loop.o pcep_socket_comm.o
OBJ = $(patsubst %,$(OBJ_DIR)/%,$(_OBJ))
OBJ_MOCK = $(OBJ_DIR)/pcep_socket_comm_mock.o

_TEST_OBJ = pcep_socket_comm_tests.o pcep_socket_comm_test.o pcep_socket_comm_loop_test.o
TEST_OBJ = $(patsubst %,$(TEST_DIR)/%,$(_TEST_OBJ))

all: $(LIB) $(LIB_MOCK) $(TEST_BIN)

$(LIB): $(OBJ)
	$(shell [ ! -d $(@D) ] && mkdir -p $(@D))
	$(AR) $(ARFLAGS) $@ $^ 
	$(RANLIB) $@

$(LIB_MOCK): $(OBJ_MOCK)
	$(shell [ ! -d $(@D) ] && mkdir -p $(@D))
	$(AR) $(ARFLAGS) $@ $^ 
	$(RANLIB) $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS) $(EXTERNAL_DEPS)
	$(shell [ ! -d $(@D) ] && mkdir -p $(@D))
	$(CC) -c -o $@ $< $(CFLAGS) $(COVERAGE_FLAGS)

$(TEST_BIN): $(TEST_OBJ) $(LIB)
	$(CC) -o $@ $(TEST_OBJ) $(CFLAGS) $(TEST_LIB_DIRS) $(TEST_LIBS) $(COVERAGE_FLAGS)

$(TEST_DIR)/%.o: $(TEST_DIR)/%.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) -I$(SRC_DIR) $(COVERAGE_FLAGS)

.PHONY: all clean test

test: $(TEST_BIN)
	$(TEST_BIN)
	$(VALGRIND) --log-file=valgrind.$(LIB_NAME).log $(TEST_BIN) || ({ echo "Valgrind memory check error"; exit 1; })

clean:
	rm -f $(LIB) $(LIB_MOCK) $(TEST_BIN) $(OBJ_DIR)/*.o $(TEST_DIR)/*.o valgrind*.log *~ core $(INC_DIR)/*~ $(SRC_DIR)/*~

