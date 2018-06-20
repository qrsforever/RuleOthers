# 通用 Makefile

# $(info "######## Makefile #######")

# 其他变量
OPTIMIZE :=
WARNINGS := -Wall -Wno-unused -Wno-format
DEFS     := -DDEBUG -DUSE_SHARED_PTR

PROJECT_ROOT_DIR := $(shell git worktree list | cut -d \  -f1)
IS_HOMEBRAIN := $(shell git config --get remote.origin.url | grep -o smarthome)

ifeq ($(IS_HOMEBRAIN), smarthome)
	UTILS_DIR := ../utils
	CLIPS_DIR := $(PROJECT_ROOT_DIR)/homebrain/external/clips/core
	CLIPS_LIB := $(PROJECT_ROOT_DIR)/out/linux/x86_64/release
else
	UTILS_DIR := Utils
	CLIPS_DIR := /workspace/clips/learn/clips_core_source_630/core
	CLIPS_LIB := $(CLIPS_DIR)
	DRIVE_DIR := RuleDriver
endif

CLIPSCPP_DIR:= Clipscpp
DRIVER_DIR  := RuleDriver
MISC_DIR 	:= $(UTILS_DIR)/Misc
MESSAGE_DIR := $(UTILS_DIR)/Message
LOG_DIR 	:= $(UTILS_DIR)/Log

# 初始化编译工具以及编译选项
CROSS_COMPILE =
GCC 	:= $(CROSS_COMPILE)gcc
CXX 	:= $(CROSS_COMPILE)g++
CC		:=
AR		:= $(CROSS_COMPILE)ar
CFLAGS  := $(OPTIMIZE) $(WARNINGS) $(DEFS)
CPPFLAGS:= -std=c++11 -lRuleDriver -lClipscpp -lclips -lUtils_log -lUtils_message -lUtils_misc -lpthread
LDFLAGS := -L$(DRIVER_DIR)/output -L$(CLIPSCPP_DIR)/output -L$(CLIPS_LIB) -L$(MISC_DIR)/output -L$(MESSAGE_DIR)/output -L$(LOG_DIR)/output
INCLUDE := -I$(CLIPSCPP_DIR)/src -I$(MISC_DIR)/src -I$(MESSAGE_DIR)/src -I$(LOG_DIR)/src -I$(DRIVER_DIR)/src

# 源文件可能的后缀
SRCEXTS := c C cc cpp CPP c++ cxx cp
HDREXTS := h H hh hpp HPP h++ hxx hp

# 指定源文件目录, 以及目标文件生成目录
SRC_DIR = . src
INC_DIR = . src
OUT_DIR = output
OBJ_DIR = $(OUT_DIR)/obj

# 指定运行环境
RPATH = ":$(OUT_DIR):$(CLIPS_DIR)"

# 额外增加的源文件或者排除不编译的源文件
SPECIAL_SRC :=
EXCLUDE_SRC := %UnitTest.cpp %HomeBrainMain.cpp

# 设置目标类型(exe, a, so), 及目标名字
TARGET_TYPE := a
TARGET_TYPE := $(strip $(TARGET_TYPE))
TARGET_NAME :=

ifeq ($(TARGET_NAME), )
	TARGET_NAME := $(shell basename $(PWD))
endif

ifneq ($(TARGET_TYPE), exe)
	TARGET_NAME := $(join $(OUT_DIR)/lib, $(TARGET_NAME))
	TARGET_NAME := $(join $(TARGET_NAME), .$(TARGET_TYPE))
else
	TARGET_NAME := $(join $(OUT_DIR), /$(TARGET_NAME))
	LDFLAGS += -L$(OUT_DIR)
endif

# 追加搜索目录
SRC_DIR	+= $(dir $(SPECIAL_SRC))
INC_DIR += $(SRC_DIR)

# 源文件, 头文件, 链接文件
SOURCES = $(filter-out $(EXCLUDE_SRC), $(foreach d, $(SRC_DIR), $(wildcard $(addprefix $(d)/*., $(SRCEXTS)))))
HEADERS = $(foreach d, $(INC_DIR), $(wildcard $(addprefix $(d)/*., $(HDREXTS))))
OBJECTS = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(notdir $(SOURCES)))))

# 配置编译选项
INCLUDE += $(foreach d, $(sort $(INC_DIR)), -I$d)
ifeq ($(TARGET_TYPE), so)
	CFLAGS  += -fpic -shared
	LDFLAGS += -shared
endif

# 定义伪目标
PHONY = all .mkdir clean

all: .mkdir $(TARGET_NAME) test

# 函数: 添加%.x依赖文件的路径
define add_vpath
$1

endef

# 函数: 生成中间文件
define gen_o_cmd
$2/%.o: %.$1
ifeq ($3 , $(GCC))
	$(3) $(INCLUDE) -Wp,-MT,$$@ -Wp,-MMD,$$@.d $(CFLAGS) -c -o $$@ $$<
else
	$(3) $(INCLUDE) -Wp,-MT,$$@ -Wp,-MMD,$$@.d $(CFLAGS) $(CPPFLAGS) -c -o $$@ $$<
endif

endef

# 执行函数, make会将函数里的内容当Makefile解析, 注意$$符号
$(eval $(foreach i, $(SRCEXTS), $(foreach d, $(SRC_DIR), $(call add_vpath,vpath %.$i $d))))
$(eval $(foreach i, $(SRCEXTS), $(call gen_o_cmd,$i,$(OBJ_DIR),$(if $(filter-out c C, $(i)),$(CXX),$(GCC)))))

# 静态, 动态, 可执行
ifeq ($(TARGET_TYPE), a)
$(TARGET_NAME): .mkdir $(OBJECTS)
	rm -f $@
	$(AR) rcvs $@ $(OBJECTS)
else
$(TARGET_NAME): LD=$(if $(filter-out %.c %.C, $(SOURCES)),$(CXX),$(GCC))
$(TARGET_NAME): .mkdir $(OBJECTS)
	$(LD) $(OBJECTS) -o $@ $(LDFLAGS)
endif

.mkdir:
	@if [ ! -d $(OBJ_DIR) ]; then mkdir -p $(OBJ_DIR); fi

clean:
	rm -rf $(OUT_DIR)

run:$(TARGET_NAME)
	@$(TARGET_NAME)

test:$(TARGET_NAME)
	$(CXX) $(INCLUDE) $(OBJECTS) HomeBrainMain.cpp -o $(OUT_DIR)/$@ $(LDFLAGS) $(CPPFLAGS) ${CFLAGS} -Wl,-rpath=$(RPATH)
	$(OUT_DIR)/$@

.PHONY: $(PHONY)
