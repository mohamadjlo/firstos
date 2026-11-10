#include <iostream>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

#include "FileSignatureParser.h"
#include "FileExplorer.h"
#include "ImageDecoder.h"
#include "MediaPlayer.h"
#include "c_abi.h"

void test_file_signature_parser() {
    std::cout << "[TEST] Running FileSignatureParser tests..." << std::endl;
    namespace fs = std::filesystem;

    fs::create_directories("test_tmp");

    // Test PNG signature
    {
        std::ofstream f("test_tmp/sample.png", std::ios::binary);
        unsigned char png_magic[4] = {0x89, 0x50, 0x4E, 0x47};
        f.write(reinterpret_cast<char*>(png_magic), 4);
    }
    assert(FileSignatureParser::detectMimeType("test_tmp/sample.png") == MimeType::Image_PNG);

    // Test JPG signature
    {
        std::ofstream f("test_tmp/sample.jpg", std::ios::binary);
        unsigned char jpg_magic[4] = {0xFF, 0xD8, 0xFF, 0xE0};
        f.write(reinterpret_cast<char*>(jpg_magic), 4);
    }
    assert(FileSignatureParser::detectMimeType("test_tmp/sample.jpg") == MimeType::Image_JPG);

    // Test ELF signature
    {
        std::ofstream f("test_tmp/sample.elf", std::ios::binary);
        unsigned char elf_magic[4] = {0x7F, 'E', 'L', 'F'};
        f.write(reinterpret_cast<char*>(elf_magic), 4);
    }
    assert(FileSignatureParser::detectMimeType("test_tmp/sample.elf") == MimeType::Executable_ELF);

    // Test Unknown signature
    {
        std::ofstream f("test_tmp/sample.txt", std::ios::binary);
        f << "Hello, World!";
    }
    assert(FileSignatureParser::detectMimeType("test_tmp/sample.txt") == MimeType::Unknown);

    fs::remove_all("test_tmp");
    std::cout << "  -> FileSignatureParser tests PASSED." << std::endl;
}

void test_file_explorer() {
    std::cout << "[TEST] Running FileExplorer tests..." << std::endl;
    namespace fs = std::filesystem;

    fs::create_directories("test_explorer_dir");
    std::ofstream("test_explorer_dir/document.txt") << "doc";
    std::ofstream("test_explorer_dir/image.png") << "img";
    std::ofstream("test_explorer_dir/app.elf") << "elf";

    FileExplorer explorer;
    explorer.setCurrentDirectory("test_explorer_dir");
    assert(explorer.getCurrentFiles().size() == 3);

    // Test synchronous filter
    explorer.filterFilesSync("image");
    auto filtered = explorer.getFilteredFiles();
    assert(filtered.size() == 1);
    assert(filtered[0].filename() == "image.png");

    // Test asynchronous filter
    explorer.filterFilesAsync("doc");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    filtered = explorer.getFilteredFiles();
    assert(filtered.size() == 1);
    assert(filtered[0].filename() == "document.txt");

    fs::remove_all("test_explorer_dir");
    std::cout << "  -> FileExplorer tests PASSED." << std::endl;
}

void test_image_decoder() {
    std::cout << "[TEST] Running ImageDecoder tests..." << std::endl;
    ImageDecoder decoder;

    // Test exception safety on empty path
    bool ok_empty = decoder.load("");
    assert(!ok_empty);
    // Returns 1x1 red placeholder
    assert(decoder.getWidth() == 1);
    assert(decoder.getHeight() == 1);
    assert(decoder.getNextFrame().size() == 4);

    // Test invalid file
    bool ok_nonexistent = decoder.load("nonexistent_file_12345.xyz");
    assert(!ok_nonexistent);
    assert(decoder.getWidth() == 1);
    assert(decoder.getHeight() == 1);

    std::cout << "  -> ImageDecoder tests PASSED." << std::endl;
}

void test_media_player() {
    std::cout << "[TEST] Running MediaPlayer tests..." << std::endl;
    {
        MediaPlayer player;
        assert(player.getState() == PlaybackState::STOPPED);

        player.play();
        assert(player.getState() == PlaybackState::PLAYING);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        player.pause();
        assert(player.getState() == PlaybackState::PAUSED);

        player.stop();
        assert(player.getState() == PlaybackState::STOPPED);
    } // Verifies clean destructor thread join

    std::cout << "  -> MediaPlayer tests PASSED." << std::endl;
}

void test_c_abi() {
    std::cout << "[TEST] Running C-ABI interop tests..." << std::endl;

    // Explorer C-ABI
    ExplorerHandle exp = Explorer_Create();
    assert(exp != nullptr);
    Explorer_SetSearchFilter(exp, "test");
    Explorer_Destroy(exp);

    // Media Player C-ABI
    MediaPlayerHandle mp = MediaPlayer_Create();
    assert(mp != nullptr);
    MediaPlayer_LoadFile(mp, "some_file.png");
    MediaPlayer_Destroy(mp);

    std::cout << "  -> C-ABI interop tests PASSED." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  FirstOS Modern C++ Application Tests  " << std::endl;
    std::cout << "========================================" << std::endl;

    test_file_signature_parser();
    test_file_explorer();
    test_image_decoder();
    test_media_player();
    test_c_abi();

    std::cout << "========================================" << std::endl;
    std::cout << "  ALL C++ TESTS PASSED SUCCESSFULLY!    " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
