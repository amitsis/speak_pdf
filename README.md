# speakpdf

`speakpdf` converts OCR-readable PDFs, UTF-8 text files, or direct text into OPUS speech audio using the OpenAI Text-to-Speech API.

## Install from source

```sh
make
sudo make install
sudo mandb
man speakpdf
```

Default paths:

- `/usr/bin/speakpdf`
- `/usr/share/man/man1/speakpdf.1.gz`

## Build RPM on openSUSE

```sh
sudo zypper install rpm-build rpmdevtools
rpmdev-setuptree
tar czf speakpdf-1.1.tar.gz speakpdf-1.1
cp speakpdf-1.1.tar.gz ~/rpmbuild/SOURCES/
cp speakpdf-1.1/packaging/rpm/speakpdf.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/speakpdf.spec
```
