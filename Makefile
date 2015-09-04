CXX = c++ -std=c++11 -stdlib=libc++
CPPFLAGS = -I/usr/local/include/js
CXXFLAGS = -g
#LDFLAGS = -L/usr/local/lib -lmozjs185
LDFLAGS =

jsvm: jsvm.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< libmozjs_debug.a -o $@

clean:
	rm -f jsvm

force: clean jsvm
