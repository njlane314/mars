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
SRC = src
BUILDDIR ?= build
OBJDIR = $(BUILDDIR)/obj
PROG = $(BUILDDIR)/mars

OBJS = $(OBJDIR)/main.o $(OBJDIR)/core.o $(OBJDIR)/data.o $(OBJDIR)/features.o $(OBJDIR)/target.o $(OBJDIR)/cost.o $(OBJDIR)/scale.o $(OBJDIR)/hmm.o $(OBJDIR)/alpha.o $(OBJDIR)/backtest.o $(OBJDIR)/store.o $(OBJDIR)/report.o $(OBJDIR)/db.o $(OBJDIR)/dex.o

all: $(PROG)

$(PROG): $(OBJS) | $(BUILDDIR)
	$(CXX) -o $(PROG) $(OBJS) $(LDFLAGS)

$(BUILDDIR) $(OBJDIR):
	mkdir -p "$@"

$(OBJDIR)/main.o: $(SRC)/main.c $(SRC)/api.h | $(OBJDIR)
	$(CC) $(CFLAGS) -c $(SRC)/main.c -o $(OBJDIR)/main.o

$(OBJDIR)/core.o: $(SRC)/core.cpp $(SRC)/api.h $(SRC)/alpha.hpp $(SRC)/backtest.hpp $(SRC)/data.hpp $(SRC)/features.hpp $(SRC)/hmm.hpp $(SRC)/store.hpp $(SRC)/report.hpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/core.cpp -o $(OBJDIR)/core.o

$(OBJDIR)/data.o: $(SRC)/data.cpp $(SRC)/data.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/data.cpp -o $(OBJDIR)/data.o

$(OBJDIR)/features.o: $(SRC)/features.cpp $(SRC)/features.hpp $(SRC)/target.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/features.cpp -o $(OBJDIR)/features.o

$(OBJDIR)/target.o: $(SRC)/target.cpp $(SRC)/target.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/target.cpp -o $(OBJDIR)/target.o

$(OBJDIR)/cost.o: $(SRC)/cost.cpp $(SRC)/cost.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/cost.cpp -o $(OBJDIR)/cost.o

$(OBJDIR)/scale.o: $(SRC)/scale.cpp $(SRC)/scale.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/scale.cpp -o $(OBJDIR)/scale.o

$(OBJDIR)/hmm.o: $(SRC)/hmm.cpp $(SRC)/hmm.hpp $(SRC)/scale.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/hmm.cpp -o $(OBJDIR)/hmm.o

$(OBJDIR)/alpha.o: $(SRC)/alpha.cpp $(SRC)/alpha.hpp $(SRC)/backtest.hpp $(SRC)/hmm.hpp $(SRC)/scale.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/alpha.cpp -o $(OBJDIR)/alpha.o

$(OBJDIR)/backtest.o: $(SRC)/backtest.cpp $(SRC)/backtest.hpp $(SRC)/cost.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/backtest.cpp -o $(OBJDIR)/backtest.o

$(OBJDIR)/store.o: $(SRC)/store.cpp $(SRC)/store.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/store.cpp -o $(OBJDIR)/store.o

$(OBJDIR)/report.o: $(SRC)/report.cpp $(SRC)/report.hpp $(SRC)/store.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/report.cpp -o $(OBJDIR)/report.o

$(OBJDIR)/db.o: $(SRC)/db.cpp $(SRC)/api.h $(SRC)/dex.hpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/db.cpp -o $(OBJDIR)/db.o

$(OBJDIR)/dex.o: $(SRC)/dex.cpp $(SRC)/dex.hpp $(SRC)/api.h | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $(SRC)/dex.cpp -o $(OBJDIR)/dex.o

install: $(PROG)
	$(INSTALL) -d "$(DESTDIR)$(BINDIR)"
	$(INSTALL) -m 0755 $(PROG) "$(DESTDIR)$(BINDIR)/mars"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/mars"

clean:
	rm -rf $(BUILDDIR)
	rm -f mars *.o

.PHONY: all clean install uninstall
