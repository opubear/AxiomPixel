#include "Runtime/System/IO/FileIO.h"
#include "Runtime/System/IO/StorageError.h"
#include "Runtime/System/Serialization/TextArchive.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
using namespace pixel;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class Operation> void Reject(Operation operation) {
    try { operation(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("malformed archive was accepted");
}
void TextArchive() {
    serialization::Writer<1024> writer;
    const std::string text("first\n\0last", 11);
    writer.Numbers(-42, 0.125);
    writer.String(text);
    const auto bytes = writer.Finish();
    serialization::Reader<1024> reader(bytes);
    Check(reader.Number<int>() == -42 && reader.Number<double>() == 0.125 && reader.String(32) == text,
        "archive lost scalar precision or string bytes");
    reader.End();
    Reject([] { serialization::Reader<4> value("12345"); });
    Reject([] { serialization::Reader<1024>("nan\n").Number<double>(); });
    Reject([] { serialization::Reader<1024>("2\n").Boolean(); });
    Reject([] { serialization::Reader<1024>("4\nabc\n").String(8); });
    Reject([] { serialization::Reader<1024>("leftover").End(); });
}
void FilePublication() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("pixel-system-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path Directory;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(Directory, ignored); }
    } cleanup{directory};
    const auto path = directory / "value";
    io::CommitFile(path, [](auto &out) { out << "committed"; });
    const auto before = io::DigestFile(path);
    try {
        io::CommitFile(path, [](auto &out) { out << "partial"; throw std::runtime_error("writer failure"); });
    } catch (const std::runtime_error &) {}
    Check(io::DigestFile(path) == before, "failed writer replaced the committed file");
    io::CommitFile(path, [](auto &out) { out << "replacement"; });
    Check(io::DigestFile(path) != before && !std::filesystem::exists(path.string() + ".tmp"),
        "successful writer did not publish and consume temporary file");
    Check(io::CaptureFailure([&] { io::RequireFile(directory / "absent"); }).has_value(), "storage error escaped I/O boundary");
    bool programmerError = false;
    try { io::CaptureFailure([] { throw std::logic_error("programmer error"); }); }
    catch (const std::logic_error &) { programmerError = true; }
    Check(programmerError, "I/O boundary swallowed a programming error");
}
} // namespace
int main() {
    try { TextArchive(); FilePublication(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
