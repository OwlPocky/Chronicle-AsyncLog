# 定义目标文件名
TARGET = test

# 定义源文件和头文件路径（若有多个文件可空格分隔）
SRC =./test.cpp
INC = -I../src

# C++ 编译器和选项
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 $(INC)  # 编译选项（警告、C++11标准、头文件路径）
LDFLAGS = -ljsoncpp -pthread                  # 链接jsoncpp库和pthread库

# 目标文件生成规则
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

# 清理规则
clean:
	rm -f $(TARGET)
	rm -rf ./logfile/ ./test1/
