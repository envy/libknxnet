CXXFLAGS += -Wall -Wextra -std=c++11 -fpic -fstack-protector-strong -march=native -O2 -g
LDFLAGS += -shared

all: libknxnet.a libknxnet.so

libknxnet.a: knxnet.o
	ar r $@ $^

libknxnet.so: knxnet.o
	$(CXX) $(LDFLAGS) $^ -o $@

knxnet.o: knxnet.cpp knxnet.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf *.o *.a *.so
