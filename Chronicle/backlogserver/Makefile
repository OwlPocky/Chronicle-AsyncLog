# 定义目标文件名
TARGET = BackLogServer

# 定义源文件和头文件路径
SRC = Server.cpp

# C++ 编译器和选项
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11  # 编译选项（警告、C++11标准）
LDFLAGS = -pthread                  # 链接pthread库

# 目标文件生成规则
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

# 清理规则
clean:
	rm -f $(TARGET)
	rm -f logfile.log
