NAME      = speak_pdf
VERSION   = 1.1

PREFIX    ?= /usr
BINDIR    ?= $(PREFIX)/bin
MANDIR    ?= $(PREFIX)/share/man
MAN1DIR   ?= $(MANDIR)/man1

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS    ?= -lcurl

INSTALL   ?= install

TARGET    = $(NAME)
SOURCE    = src/$(NAME).cpp

all: $(TARGET) man/$(NAME).1.gz

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) $(LDLIBS) -o $(TARGET)

man/$(NAME).1.gz: man/$(NAME).1
	gzip -9 -c $< > $@

check:
	test -x ./$(TARGET)
	grep -q '^\.TH SPEAK_PDF 1' man/$(NAME).1
	gzip -t man/$(NAME).1.gz
	@echo "Basic checks passed."

install: all
	$(INSTALL) -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)
	$(INSTALL) -Dm644 man/$(NAME).1.gz $(DESTDIR)$(MAN1DIR)/$(NAME).1.gz

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(MAN1DIR)/$(NAME).1.gz

dist: clean
	cd .. && tar --exclude-vcs -czf $(NAME)-$(VERSION).tar.gz $(NAME)-$(VERSION)

rpm: dist
	rpmdev-setuptree
	cp ../$(NAME)-$(VERSION).tar.gz $(HOME)/rpmbuild/SOURCES/
	cp packaging/rpm/$(NAME).spec $(HOME)/rpmbuild/SPECS/
	rpmbuild -ba $(HOME)/rpmbuild/SPECS/$(NAME).spec

clean:
	rm -f $(TARGET)
	rm -f man/$(NAME).1.gz

.PHONY: all check install uninstall dist rpm clean
