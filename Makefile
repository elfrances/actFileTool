###########################################################################
#
#   Filename:           Makefile
#
#   Author:             Marcelo Mourier
#   Created:            Sun, Mar 14, 2021 10:10:04 PM
#
#   Description:        This makefile is used to build the gpxFileTool
#
#
#
###########################################################################
#
#                  Copyright (c) 2021 Marcelo Mourier
#
###########################################################################

BIN_DIR = .
DEP_DIR = .
OBJ_DIR = .

CFLAGS = -D_GNU_SOURCE -I. -ggdb -Wall -Werror -O3
LDFLAGS = -ggdb 

SOURCES = $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS := $(patsubst %.c,$(DEP_DIR)/%.d,$(SOURCES))

# Rule to autogenerate dependencies files
$(DEP_DIR)/%.d: %.c
	@set -e; $(RM) $@; \
         $(CC) -MM $(CPPFLAGS) $< > $@.temp; \
         sed 's,\($*\)\.o[ :]*,$(OBJ_DIR)\/\1.o $@ : ,g' < $@.temp > $@; \
         $(RM) $@.temp

# Rule to generate object files
$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

all: gpxFileTool

gpxFileTool: $(OBJECTS) Makefile
	$(RM) build_info.c
	$(SHELL) -ec 'echo "const char *buildInfo = \"built on `date` by `whoami`@`hostname`\";" >> build_info.c'
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/build_info.o -c build_info.c
	$(RM) build_info.c
	$(CC) $(LDFLAGS) -o $(BIN_DIR)/$@ $(OBJECTS) $(OBJ_DIR)/build_info.o -lm

clean:
	$(RM) $(OBJECTS) $(OBJ_DIR)/build_info.o $(DEP_DIR)/*.d $(BIN_DIR)/gpxFileTool

include $(DEPS)

