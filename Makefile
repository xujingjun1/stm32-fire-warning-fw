# 帧解析器单元测试 —— make test 即可编译运行，无需单片机
CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra
INCS     = -Iprotocol
BUILD    = test_frame_logic

.PHONY: test clean

test: $(BUILD)
	./$(BUILD)

$(BUILD): tests/test_frame_logic.c protocol/frame_parser.c protocol/frame_parser.h
	$(CC) $(CFLAGS) $(INCS) -o $@ tests/test_frame_logic.c protocol/frame_parser.c

stdin_parse: tools/stdin_parse.c protocol/frame_parser.c protocol/frame_parser.h
	$(CC) $(CFLAGS) $(INCS) -o $@ tools/stdin_parse.c protocol/frame_parser.c

clean:
	rm -f $(BUILD) $(BUILD).exe stdin_parse stdin_parse.exe
