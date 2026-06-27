Name:           speak_pdf
Version:        1.1
Release:        1%{?dist}
Summary:        Read OCR PDFs and text files aloud using OpenAI TTS

License:        GPLv2
URL:            https://example.invalid/speak_pdf
Source0:        %{name}-%{version}.tar.gz

BuildArch:      noarch
BuildRequires:  gzip
BuildRequires:  python3

Requires:       python3
Requires:       python3-openai
Requires:       ffmpeg
Requires:       mpv
Requires:       poppler-tools
Recommends:     python3-pypdf

%description
speak_pdf converts OCR-readable PDF documents, UTF-8 text files, or direct text
into spoken OPUS audio using the OpenAI Text-to-Speech API. Long texts are
automatically split into multiple requests and recombined into one OPUS file.

%prep
%autosetup

%build
make

%install
%make_install PREFIX=%{_prefix}

%check
make check

%files
%license LICENSE
%doc README.md INSTALL NEWS AUTHORS CHANGELOG.md
%{_bindir}/speak_pdf
%{_mandir}/man1/speak_pdf.1%{?ext_man}

%changelog
* Fri Jun 26 2026 Andreas Mitsis <amitsis@gmail.com> - 1.1-1
- Initial RPM package for openSUSE Tumbleweed.
