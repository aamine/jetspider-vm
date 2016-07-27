CXX = c++ -std=c++11 -stdlib=libc++
CXXFLAGS = -g -Os
CPPFLAGS = -I/usr/local/include/js
LDFLAGS = -L/usr/local/lib -lmozjs185-1.0

jsvm: jsvm.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f jsvm

force: clean jsvm
