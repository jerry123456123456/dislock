CC = gcc 
CCC = g++
CXX = g++

#这是 C++ 编译的基本选项。-g 用于在编译时加入调试信息；-O 开启优化；-Wall 会开启所有常见的编译警告；-fPIC 生成位置无关代码，常用于生成共享库；-Wno-reorder 则是关闭成员变量初始化顺序的警告
BASICOPTS = -g -O -Wall -fPIC -Wno-reorder
#C 语言编译的基本选项，和 BASICOPTS 类似，不过没有 -Wno-reorder 选项，因为 C 语言没有成员变量初始化顺序的问题
CBASICOPTS = -g -O -Wall -fPIC
#CFLAGS：C 语言编译选项，这里引用了 CBASICOPTS
CFLAGS = $(CBASICOPTS)
#CCFLAGS 和 CXXFLAGS：C++ 编译选项，引用了 BASICOPTS
CCFLAGS = $(BASICOPTS)
CXXFLAGS = $(BASICOPTS)
#当前为空，可能用于后续添加管理相关的编译选项
CCADMIN = 

#指定 C++ 编译时的头文件搜索路径，包含当前目录、/usr/local/include/、./redlock-cpp/ 和 ./hiredis/
INCLUDE = -I./ -I/usr/local/include/ -I./redlock-cpp/ -I./hiredis/
#CINCLUDE：指定 C 语言编译时的头文件搜索路径，包含当前目录和 /usr/local/include/
CINCLUDE = -I./ -I/usr/local/include/ 
#LOCKLIB：指定链接时的库搜索路径和要链接的库。-L 后面跟着库文件所在目录，-l 后面是要链接的库名。这里指定了 ./bin 目录下的 redlock 库和 ./hiredis 目录下的 hiredis 库
LOCKLIB = -L./bin -lredlock -L./hiredis -lhiredis

#TARGETDIR_BIN：指定生成文件的目标目录为 bin
TARGETDIR_BIN = bin
#OUTPUT：定义要生成的静态库文件名 libredlock.a
OUTPUT = libredlock.a
#EXOUTPUT 和 EXOUTPUTCLOCK：分别定义两个可执行文件的文件名 LockExample 和 CLockExample
EXOUTPUT = LockExample
EXOUTPUTCLOCK = CLockExample

#all 是默认目标，依赖于 bin 目录下的静态库 libredlock.a 以及两个可执行文件 LockExample 和 CLockExample
all: $(TARGETDIR_BIN)/$(OUTPUT) $(TARGETDIR_BIN)/$(EXOUTPUT) $(TARGETDIR_BIN)/$(EXOUTPUTCLOCK)

#OBJS_libcomm：列出了生成静态库 libredlock.a 所需的目标文件
OBJS_libcomm = \
    	$(TARGETDIR_BIN)/sds.o \
    	$(TARGETDIR_BIN)/redlock.o

#EXOBJS：列出了生成可执行文件 LockExample 所需的目标文件
EXOBJS =  \
    	$(TARGETDIR_BIN)/LockExample.o\
    	$(TARGETDIR_BIN)/sds.o\
   	$(TARGETDIR_BIN)/redlock.o

#EXOBJSCLOCK：列出了生成可执行文件 CLockExample 所需的目标文件
EXOBJSCLOCK =  \
	$(TARGETDIR_BIN)/CLockExample.o\
    	$(TARGETDIR_BIN)/sds.o\
    	$(TARGETDIR_BIN)/redlock.o

#ARCPP：定义了创建静态库的命令，$(AR) 是静态库创建工具（通常是 ar），$(ARFLAGS) 是 ar 的选项，$@ 代表当前目标
ARCPP = $(AR) $(ARFLAGS) $@
$(TARGETDIR_BIN)/$(OUTPUT): $(TARGETDIR_BIN) $(OBJS_libcomm)
	$(ARCPP) $(OBJS_libcomm)
#$(TARGETDIR_BIN)/$(EXOUTPUT)：目标是生成可执行文件 LockExample，依赖于 bin 目录和 EXOBJS 中的目标文件，使用 g++ 编译器将目标文件和指定的库链接成可执行文件
$(TARGETDIR_BIN)/$(EXOUTPUT): $(TARGETDIR_BIN) $(EXOBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGETDIR_BIN)/$(EXOUTPUT) $(EXOBJS) -L./hiredis -lhiredis
#$(TARGETDIR_BIN)/$(EXOUTPUTCLOCK)：目标是生成可执行文件 CLockExample，依赖于 bin 目录和 EXOBJSCLOCK 中的目标文件，使用 g++ 编译器将目标文件和指定的库链接成可执行文件
$(TARGETDIR_BIN)/$(EXOUTPUTCLOCK): $(TARGETDIR_BIN) $(EXOBJSCLOCK)
	$(CXX) $(CXXFLAGS) -o $(TARGETDIR_BIN)/$(EXOUTPUTCLOCK) $(EXOBJSCLOCK) -L./hiredis -lhiredis

#第一条规则：如果目标文件在 bin 目录下，源文件在 ./redlock-cpp/ 目录下且为 .cpp 文件，就使用 g++ 编译器，根据 CXXFLAGS 和 INCLUDE 选项进行编译
$(TARGETDIR_BIN)/%.o : ./redlock-cpp/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE)  -o $@  -c $(filter %.cpp, $^)
#第二条规则：如果目标文件在 bin 目录下，源文件在 ./redlock-cpp/ 目录下且为 .c 文件，就使用 gcc 编译器，根据 CFLAGS 和 CINCLUDE 选项进行编译
$(TARGETDIR_BIN)/%.o : ./redlock-cpp/%.c
	$(CC) $(CFLAGS) $(CINCLUDE)  -o $@  -c $<
#第三条规则：如果目标文件在 bin 目录下，源文件在当前目录下且为 .cpp 文件，同样使用 g++ 编译器，根据 CXXFLAGS 和 INCLUDE 选项进行编译
$(TARGETDIR_BIN)/%.o : ./%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE)  -o $@  -c $(filter %.cpp, $^)

#### 清理目标，删除所生成的文件 ####
clean:
	rm -f \
        $(TARGETDIR_BIN)/$(OUTPUT) \
        $(TARGETDIR_BIN)/*.o 
	$(CCADMIN)
	rm -f -r $(TARGETDIR_BIN)

#该规则用于创建 bin 目录。如果 bin 目录不存在，mkdir -p 命令会递归创建该目录
$(TARGETDIR_BIN):
	mkdir -p $(TARGETDIR_BIN)
