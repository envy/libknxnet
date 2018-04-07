CXXFLAGS += -Wall -Wextra -std=c++11

all: libknxnet.a

libknxnet.a: knxnet.o
	ar r $@ $^

knxnet.o: knxnet.cpp knxnet.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf *.o *.a
