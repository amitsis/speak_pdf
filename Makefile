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

# Desired owner for the locally built binary and installed files.
# When building as user "mitsis", the binary is already owned correctly.
# When building/installing as root, these settings force ownership to mitsis.
OWNER     ?= mitsis
GROUP     ?= $(shell id -gn $(OWNER) 2>/dev/null || echo $(OWNER))

TARGET     = $(NAME)
SOURCE     = src/speakpdf.cpp
MANPAGE    = man/$(NAME).1
MANPAGE_GZ = $(MANPAGE).gz

all: $(TARGET) manpage

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) $(LDLIBS) -o $(TARGET)
	chmod 755 $(TARGET)
	@if [ "$$(id -u)" = "0" ]; then \
		chown $(OWNER):$(GROUP) $(TARGET); \
	elif [ "$$(id -un)" != "$(OWNER)" ]; then \
		echo "Warning: $(TARGET) is owned by $$(id -un), not $(OWNER)."; \
		echo "Run 'sudo chown $(OWNER):$(GROUP) $(TARGET)' if ownership must be changed."; \
	fi

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
	@if [ "$$(id -u)" = "0" ]; then \
		$(INSTALL) -o $(OWNER) -g $(GROUP) -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME); \
		$(INSTALL) -o $(OWNER) -g $(GROUP) -Dm755 "$(TARGET).py" "$(DESTDIR)$(BINDIR)/$(NAME).py"; \
	else \
		$(INSTALL) -Dm755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME); \
		$(INSTALL) -Dm755 "$(TARGET).py" "$(DESTDIR)$(BINDIR)/$(NAME).py"; \
	fi
	@if [ -f "$(MANPAGE_GZ)" ]; then \
		if [ "$$(id -u)" = "0" ]; then \
			$(INSTALL) -o $(OWNER) -g $(GROUP) -Dm644 "$(MANPAGE_GZ)" "$(DESTDIR)$(MAN1DIR)/$(NAME).1.gz"; \
		else \
			$(INSTALL) -Dm644 "$(MANPAGE_GZ)" "$(DESTDIR)$(MAN1DIR)/$(NAME).1.gz"; \
		fi; \
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
