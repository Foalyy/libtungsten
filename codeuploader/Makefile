## Settings

NAME=codeuploader
CXX=g++
CXXFLAGS=-std=c++11 -Wall -g
LFLAGS=-lboost_system -lusb-1.0 -lpthread
OBJS=usb.o


## RULES

.PHONY: clean

all: $(NAME)

$(NAME): $(NAME).cpp $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(LFLAGS)

clean: 
	rm -f $(NAME) *.o
