/*
 * speak_pdf.cpp
 *
 * High-quality PDF/Text to Speech converter for Linux using
 * the OpenAI Text-to-Speech API.
 *
 * Copyright (C) 2026 Andreas Mitsis
 *
 * Author: Andreas Mitsis <amitsis@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * $Id: speak_pdf.cpp,v 1.0 2026/06/27 16:45:00 mitsis Exp $
 */

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include <curl/curl.h>

namespace fs = std::filesystem;

static constexpr int DEFAULT_CHUNK_CHARS = 5500;
static const std::string PROGRAM_NAME = "speak_pdf";
static const std::string PROGRAM_VERSION = "1.1";
static const std::string DEFAULT_OUTPUT = "speech.opus";
static const std::string DEFAULT_TEXT_EXPORT = "extracted.txt";

static const char *COPYRIGHT_NOTICE =
    "Copyright (C) 2026 Andreas Mitsis\n"
    "License GPLv3+: GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl-3.0.html>\n"
    "This is free software: you are free to change and redistribute it.\n"
    "There is NO WARRANTY, to the extent permitted by law.\n";

struct PageRange {
    int start = 1;
    int end = 1;
};

struct Options {
    std::string input;
    std::string output = DEFAULT_OUTPUT;
    std::string voice = "onyx";
    std::string model = "gpt-4o-mini-tts";
    std::optional<PageRange> pages;
    std::optional<std::string> export_text;
    int chunk_chars = DEFAULT_CHUNK_CHARS;
    bool no_play = false;
};

static std::string trim(const std::string &s) {
    const auto first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, last - first + 1);
}

static bool has_suffix_case_insensitive(std::string s, std::string suffix) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool command_exists(const std::string &name) {
    const char *path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::stringstream ss(path_env);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        fs::path p = fs::path(dir) / name;
        if (::access(p.c_str(), X_OK) == 0) return true;
    }
    return false;
}

static std::string shell_quote(const fs::path &p) {
    std::string s = p.string();
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

static std::string normalize_text(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\0'), text.end());
    text = std::regex_replace(text, std::regex("\r\n"), "\n");
    text = std::regex_replace(text, std::regex("\r"), "\n");
    text = std::regex_replace(text, std::regex(R"(([[:alnum:]_])-\n([[:alnum:]_]))"), "$1$2");
    text = std::regex_replace(text, std::regex(R"([ \t]+)"), " ");
    text = std::regex_replace(text, std::regex(R"(\n{3,})"), "\n\n");
    return trim(text);
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read file: " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void write_file(const fs::path &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write file: " + path.string());
    out << content;
}

static std::optional<PageRange> parse_pages(const std::string &value) {
    std::smatch m;
    static const std::regex rx(R"(^\s*(\d+)(?:\s*[-:]\s*(\d+))?\s*$)");
    if (!std::regex_match(value, m, rx)) {
        throw std::runtime_error("Use page syntax such as 3 or 3-7.");
    }
    int start = std::stoi(m[1].str());
    int end = m[2].matched ? std::stoi(m[2].str()) : start;
    if (start < 1 || end < start) {
        throw std::runtime_error("Page ranges must be positive and ascending.");
    }
    return PageRange{start, end};
}

static std::string run_command_capture(const std::string &cmd) {
    FILE *pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("Failed to run command: " + cmd);

    std::string output;
    char buffer[8192];
    while (std::fgets(buffer, sizeof(buffer), pipe)) output += buffer;

    int status = ::pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("Command failed: " + cmd);
    }
    return output;
}

static std::string extract_pdf_text(const fs::path &path, const std::optional<PageRange> &pages) {
    if (!command_exists("pdftotext")) {
        throw std::runtime_error("pdftotext is not installed. Install it with: sudo zypper install poppler-tools");
    }

    std::ostringstream cmd;
    cmd << "pdftotext -layout ";
    if (pages) cmd << "-f " << pages->start << " -l " << pages->end << " ";
    cmd << shell_quote(path) << " -";

    std::string text = normalize_text(run_command_capture(cmd.str()));
    if (text.empty()) {
        throw std::runtime_error("No readable text was extracted from the PDF. Make sure it has an OCR text layer, e.g. via OCRmyPDF.");
    }
    return text;
}

static std::string load_input(const std::string &arg, const std::optional<PageRange> &pages) {
    fs::path path(arg);
    if (fs::is_regular_file(path) && has_suffix_case_insensitive(path.extension().string(), ".pdf")) {
        std::cout << "Extracting readable OCR/PDF text layer from: " << path << "\n";
        return extract_pdf_text(path, pages);
    }
    if (fs::is_regular_file(path)) {
        std::cout << "Reading text file: " << path << "\n";
        return normalize_text(read_file(path));
    }
    return normalize_text(arg);
}

static std::vector<std::string> split_regex(const std::string &text, const std::regex &rx) {
    std::sregex_token_iterator it(text.begin(), text.end(), rx, -1), end;
    std::vector<std::string> parts;
    for (; it != end; ++it) parts.push_back(it->str());
    return parts;
}


static std::vector<std::string> split_sentences(const std::string &paragraph) {
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < paragraph.size(); ++i) {
        char c = paragraph[i];
        current += c;
        if ((c == '.' || c == '!' || c == '?' || c == ';' || c == ':') &&
            i + 1 < paragraph.size() && std::isspace(static_cast<unsigned char>(paragraph[i + 1]))) {
            std::string t = trim(current);
            if (!t.empty()) out.push_back(t);
            current.clear();
            while (i + 1 < paragraph.size() && std::isspace(static_cast<unsigned char>(paragraph[i + 1]))) ++i;
        }
    }
    std::string t = trim(current);
    if (!t.empty()) out.push_back(t);
    return out;
}

static std::vector<std::string> split_text(const std::string &input, int max_chars) {
    std::string text = normalize_text(input);
    if (static_cast<int>(text.size()) <= max_chars) return {text};

    std::vector<std::string> chunks;
    std::string current;
    auto flush_current = [&]() {
        std::string t = trim(current);
        if (!t.empty()) chunks.push_back(t);
        current.clear();
    };

    for (std::string paragraph : split_regex(text, std::regex(R"(\n\s*\n)"))) {
        paragraph = trim(paragraph);
        if (paragraph.empty()) continue;

        if (static_cast<int>(paragraph.size()) > max_chars) {
            flush_current();
            std::vector<std::string> sentences = split_sentences(paragraph);
            std::string buffer;
            for (const std::string &sentence_raw : sentences) {
                std::string sentence = trim(sentence_raw);
                if (sentence.empty()) continue;
                if (static_cast<int>(sentence.size()) > max_chars) {
                    if (!trim(buffer).empty()) chunks.push_back(trim(buffer));
                    buffer.clear();
                    for (size_t i = 0; i < sentence.size(); i += max_chars)
                        chunks.push_back(trim(sentence.substr(i, max_chars)));
                } else if (static_cast<int>(buffer.size() + sentence.size() + 1) <= max_chars) {
                    buffer = trim(buffer + " " + sentence);
                } else {
                    chunks.push_back(trim(buffer));
                    buffer = sentence;
                }
            }
            if (!trim(buffer).empty()) chunks.push_back(trim(buffer));
            continue;
        }

        if (static_cast<int>(current.size() + paragraph.size() + 2) <= max_chars) {
            current = trim(current + "\n\n" + paragraph);
        } else {
            flush_current();
            current = paragraph;
        }
    }
    flush_current();
    return chunks;
}

static std::string json_escape(const std::string &s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    o << buf;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

struct WriteContext {
    std::ofstream *out;
    size_t downloaded = 0;
    size_t estimated_total = 128 * 1024;
};

static void print_progress(size_t downloaded, size_t estimated_total, int bar_len = 40) {
    double percent = std::min(static_cast<double>(downloaded) / static_cast<double>(estimated_total), 1.0);
    int filled = static_cast<int>(percent * bar_len);
    std::cout << "\r[" << std::string(filled, '#') << std::string(bar_len - filled, '.') << "] ";
    std::cout.width(6);
    std::cout << std::fixed;
    std::cout.precision(1);
    std::cout << percent * 100.0 << "%" << std::flush;
}

static size_t write_audio_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t bytes = size * nmemb;
    auto *ctx = static_cast<WriteContext *>(userdata);
    ctx->out->write(ptr, static_cast<std::streamsize>(bytes));
    ctx->downloaded += bytes;
    print_progress(ctx->downloaded, ctx->estimated_total);
    return bytes;
}

static void synthesize_chunk(const std::string &text, const fs::path &output_file,
                             const std::string &model, const std::string &voice,
                             const std::string &api_key) {
    std::ofstream out(output_file, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write audio part: " + output_file.string());

    std::string body = "{\"model\":\"" + json_escape(model) + "\","
                       "\"voice\":\"" + json_escape(voice) + "\","
                       "\"input\":\"" + json_escape(text) + "\","
                       "\"response_format\":\"opus\"}";

    CURL *curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    struct curl_slist *headers = nullptr;
    std::string auth = "Authorization: Bearer " + api_key;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    WriteContext ctx{&out, 0, std::max(text.size() * 18, static_cast<size_t>(128 * 1024))};

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/audio/speech");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_audio_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speak_pdf_cpp/1.0");

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    out.close();
    std::cout << "\n";

    if (res != CURLE_OK) throw std::runtime_error(std::string("OpenAI request failed: ") + curl_easy_strerror(res));
    if (http_code < 200 || http_code >= 300) throw std::runtime_error("OpenAI request failed with HTTP status " + std::to_string(http_code));
}

static void concatenate_opus(const std::vector<fs::path> &parts, const fs::path &output_file, const fs::path &tmpdir) {
    if (parts.size() == 1) {
        fs::copy_file(parts[0], output_file, fs::copy_options::overwrite_existing);
        return;
    }
    if (!command_exists("ffmpeg")) {
        throw std::runtime_error("Long input was split into several audio parts, but ffmpeg is not installed. Install it with: sudo zypper install ffmpeg");
    }

    fs::path list_path = tmpdir / "concat-list.txt";
    std::ofstream list(list_path);
    if (!list) throw std::runtime_error("Cannot write ffmpeg concat list");
    for (const auto &part : parts) list << "file '" << fs::absolute(part).string() << "'\n";
    list.close();

    std::ostringstream cmd;
    cmd << "ffmpeg -y -hide_banner -loglevel error -f concat -safe 0 -i "
        << shell_quote(list_path) << " -c copy " << shell_quote(output_file);
    int rc = std::system(cmd.str().c_str());
    if (rc != 0) throw std::runtime_error("ffmpeg failed while combining OPUS parts");
}

static void maybe_play(const fs::path &output_file, bool no_play) {
    if (no_play) return;
    if (!command_exists("mpv")) {
        std::cerr << "mpv not found. Install it with: sudo zypper install mpv\n";
        return;
    }
    std::string cmd = "mpv " + shell_quote(output_file) + " >/dev/null 2>&1 &";
    std::system(cmd.c_str());
}

static void usage(const char *prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] INPUT\n\n"
              << "Read text aloud from a PDF, text/ASCII file, or direct string using\n"
              << "the OpenAI Text-to-Speech API.\n\n"
              << COPYRIGHT_NOTICE << "\n"
              << "Options:\n"
              << "  -o, --output FILE        Output OPUS file (default: speech.opus)\n"
              << "      --voice VOICE        OpenAI TTS voice (default: onyx)\n"
              << "      --model MODEL        OpenAI TTS model (default: gpt-4o-mini-tts)\n"
              << "      --pages RANGE        PDF page range, e.g. 1, 3-7, or 10:20\n"
              << "      --export-text[=FILE] Export extracted text (default name: extracted.txt)\n"
              << "      --chunk-chars N      Maximum characters per request (default: 5500)\n"
              << "      --no-play            Do not automatically play the generated audio\n"
              << "  -h, --help               Display this help and exit\n"
              << "  -V, --version            Output version information and exit\n\n";
}

static void version() {
    std::cout << COPYRIGHT_NOTICE
              << "Written by Andreas Mitsis.\n"
	      << PROGRAM_NAME << " " << PROGRAM_VERSION << "\n";
}

static Options parse_args(int argc, char **argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need_value = [&](const std::string &name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };

        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            std::exit(0);
        } else if (a == "-V" || a == "--version") {
            version();
            std::exit(0);
        } else if (a == "-o" || a == "--output") opt.output = need_value(a);
        else if (a == "--voice") opt.voice = need_value(a);
        else if (a == "--model") opt.model = need_value(a);
        else if (a == "--pages") opt.pages = parse_pages(need_value(a));
        else if (a == "--chunk-chars") opt.chunk_chars = std::stoi(need_value(a));
        else if (a == "--no-play") opt.no_play = true;
        else if (a == "--export-text") opt.export_text = DEFAULT_TEXT_EXPORT;
        else if (a.rfind("--export-text=", 0) == 0) opt.export_text = a.substr(std::string("--export-text=").size());
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("Unknown option: " + a);
        else if (opt.input.empty()) opt.input = a;
        else throw std::runtime_error("Only one input argument is supported. Quote direct text containing spaces.");
    }

    if (opt.input.empty()) {
        usage(argv[0]);
        throw std::runtime_error("Missing input argument");
    }
    if (opt.chunk_chars < 1000) throw std::runtime_error("--chunk-chars is too small. Use at least 1000.");
    return opt;
}

int main(int argc, char **argv) {
    try {
        Options args = parse_args(argc, argv);

        const char *key = std::getenv("OPENAI_API_KEY");
        if (!key || std::string(key).empty()) {
            throw std::runtime_error("CRITICAL: Missing API key (OPENAI_API_KEY) in environment.");
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);

        std::string analyse_text = load_input(args.input, args.pages);
        if (args.export_text) {
            write_file(*args.export_text, analyse_text + "\n");
            std::cout << "Extracted text exported to: " << *args.export_text << "\n";
        }

        std::vector<std::string> chunks = split_text(analyse_text, args.chunk_chars);
        fs::path output_file(args.output);

        std::cout << "Text length: " << analyse_text.size() << " characters\n";
        std::cout << "TTS chunks:  " << chunks.size() << "\n";
        std::cout << "Generating OPUS audio...\n";

        char tmp_template[] = "/tmp/speech-parts-XXXXXX";
        char *tmp_name = ::mkdtemp(tmp_template);
        if (!tmp_name) throw std::runtime_error("mkdtemp failed");
        fs::path tmpdir(tmp_name);

        std::vector<fs::path> part_files;
        try {
            for (size_t i = 0; i < chunks.size(); ++i) {
                std::ostringstream name;
                name << "part-";
                name.width(4);
                name.fill('0');
                name << (i + 1) << ".opus";
                fs::path part = tmpdir / name.str();
                part_files.push_back(part);
                std::cout << "Chunk " << (i + 1) << "/" << chunks.size()
                          << " (" << chunks[i].size() << " characters)\n";
                synthesize_chunk(chunks[i], part, args.model, args.voice, key);
            }

            std::cout << "Combining OPUS audio parts...\n";
            concatenate_opus(part_files, output_file, tmpdir);
            fs::remove_all(tmpdir);
        } catch (...) {
            fs::remove_all(tmpdir);
            throw;
        }

        std::cout << "Audio generation complete.\n";
        std::cout << "Audio saved to: " << output_file << "\n";
        maybe_play(output_file, args.no_play);
        curl_global_cleanup();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }
}
