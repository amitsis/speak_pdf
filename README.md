# speakpdf

`speakpdf` converts OCR-readable PDFs, UTF-8 text files, or direct text into OPUS speech audio using the OpenAI Text-to-Speech API.

## Install from source

```sh
make
sudo make install
sudo mandb
man speak_pdf
```

Default paths:

- `/usr/bin/speak_pdf`
- `/usr/share/man/man1/speak_pdf.1.gz`

## Build RPM on openSUSE

```sh
sudo zypper install rpm-build rpmdevtools
rpmdev-setuptree
tar czf speak_pdf-1.1.tar.gz speak_pdf-1.1
cp speak_pdf-1.1.tar.gz ~/rpmbuild/SOURCES/
cp speak_pdf-1.1/packaging/rpm/speak_pdf.spec ~/rpmbuild/SPECS/
rpmbuild -ba ~/rpmbuild/SPECS/speak_pdf.spec
```
