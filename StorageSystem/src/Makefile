LIB_DIR := ./lib
INC_DIR := ./include  # 头文件

LDFLAGS := -L$(LIB_DIR) -lbundle -levent -ljsoncpp -lstdc++fs -lpthread

CXXFLAGS := -std=c++17 -I$(INC_DIR)

test: Test.cpp base64.cpp
	g++ -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

gdb_test: Test.cpp
	g++ -g -o $@ $^ $(CXXFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf test gdb_test ./deep_storage ./low_storage ./logfile storage.data
