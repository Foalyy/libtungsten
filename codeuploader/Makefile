## Settings
NAME=codeuploader
BUILD_PATH=../../build/codeuploader
CXX=g++
CXXFLAGS=-std=c++11 -Wall -g
LFLAGS=-lboost_system -lusb-1.0 -lpthread
OBJS=usb.o


## RULES

.PHONY: clean

all: _create_build_path $(BUILD_PATH)/$(NAME)

_create_build_path:
	@mkdir -p $(BUILD_PATH)

$(BUILD_PATH)/$(NAME): $(NAME).cpp $(addprefix $(BUILD_PATH)/,$(OBJS))
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

$(BUILD_PATH)/%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(LFLAGS)

clean: 
	rm -f $(BUILD_PATH)
