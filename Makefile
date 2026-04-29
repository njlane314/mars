CC ?= cc
CXX ?= c++
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror -pedantic
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -pedantic
LDFLAGS ?= -lm -lcurl -lsqlite3
SDKROOT ?= $(shell xcrun --show-sdk-path 2>/dev/null)
ifneq ($(SDKROOT),)
CXXFLAGS += -isystem $(SDKROOT)/usr/include/c++/v1
endif
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install

OBJS = mars.o mars_core.o csv.o features.o scale.o hmm.o alpha.o backtest.o model_store.o report.o db.o

all: mars

mars: $(OBJS)
	$(CXX) -o mars $(OBJS) $(LDFLAGS)

mars.o: mars.c mars_api.h
	$(CC) $(CFLAGS) -c mars.c

mars_core.o: mars_core.cpp mars_api.h alpha.hpp backtest.hpp csv.hpp features.hpp hmm.hpp model_store.hpp report.hpp
	$(CXX) $(CXXFLAGS) -c mars_core.cpp

csv.o: csv.cpp csv.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c csv.cpp

features.o: features.cpp features.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c features.cpp

scale.o: scale.cpp scale.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c scale.cpp

hmm.o: hmm.cpp hmm.hpp scale.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c hmm.cpp

alpha.o: alpha.cpp alpha.hpp backtest.hpp hmm.hpp scale.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c alpha.cpp

backtest.o: backtest.cpp backtest.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c backtest.cpp

model_store.o: model_store.cpp model_store.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c model_store.cpp

report.o: report.cpp report.hpp model_store.hpp mars_api.h
	$(CXX) $(CXXFLAGS) -c report.cpp

db.o: db.cpp mars_api.h
	$(CXX) $(CXXFLAGS) -c db.cpp

install: mars
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 mars "$(DESTDIR)$(BINDIR)/mars"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/mars"

clean:
	rm -f mars $(OBJS) *.model *.mars trades.csv

.PHONY: all clean install uninstall
