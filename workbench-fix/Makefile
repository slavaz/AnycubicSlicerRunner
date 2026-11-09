CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -shared -fPIC
LDLIBS ?= -ldl

TARGET = acnext-workbench-fix.so
SRC = acnext-workbench-fix.c

ifeq ($(DEBUG), 1)
    CFLAGS += -g -O0
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(TARGET)
