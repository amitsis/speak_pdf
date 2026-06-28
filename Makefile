NAME      = speakpdf
VERSION   = 1.1

PREFIX    ?= /usr
BINDIR    ?= $(PREFIX)/bin
MANDIR    ?= $(PREFIX)/share/man
MAN1DIR   ?= $(MANDIR)/man1

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS    ?= -lcurl

INSTALL   ?= install

TARGET     = $(NAME)
SOURCE     = src/speakpdf.cpp
MANPAGE    = man/$(NAME).1
MANPAGE_GZ = $(MANPAGE).gz

all: $(TARGET) manpage

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) $(LDLIBS) -o $(TARGET)

manpage:
	@if [ -f "$(MANPAGE)" ]; then \
		gzip -9 -c "$(MANPAGE)" > "$(MANPAGE_GZ)"; \
		echo "Generated $(MANPAGE_GZ)"; \
	else \
		echo "Skipping man page: $(MANPAGE) not found."; \
		echo "Create it as $(MANPAGE), or rename your existing man page accordingly."; \
	fi

check: all
	test -x ./$(TARGET)
	@if [ -f "$(MANPAGE_GZ)" ]; then \
		gzip -t "$(MANPAGE_GZ)"; \
	else \
		echo "No compressed man page found; check skipped for man page."; \
	fi
	@echo "Basic checks passed."

install: all
	$(INSTALL) -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)
	$(INSTALL) -Dm755 "$(TARGET).py" "$(DESTDIR)$(BINDIR)/$(NAME).py"
	@if [ -f "$(MANPAGE_GZ)" ]; then \
		$(INSTALL) -Dm644 "$(MANPAGE_GZ)" "$(DESTDIR)$(MAN1DIR)/$(NAME).1.gz"; \
	else \
		echo "No man page installed because $(MANPAGE_GZ) does not exist."; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(MAN1DIR)/$(NAME).1.gz


dist: clean
	cd .. && tar --exclude-vcs -czf $(NAME).tar.gz $(NAME)

rpm: dist
	rpmdev-setuptree
	cp ../$(NAME).tar.gz $(HOME)/rpmbuild/SOURCES/
	cp packaging/rpm/$(NAME).spec $(HOME)/rpmbuild/SPECS/
	rpmbuild -ba $(HOME)/rpmbuild/SPECS/$(NAME).spec

clean:
	rm -f $(TARGET)
	rm -f $(MANPAGE_GZ)

.PHONY: all manpage check install uninstall clean
