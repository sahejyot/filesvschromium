#include <base/task/thread_pool.h>
#include <base/synchronization/waitable_event.h>
#include <third_party/boringssl/src/include/openssl/sha.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\cpp-httplib\httplib.h"
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <C:/Users/Qikfox/Desktop/modular-chromium-threading/src/base/task/thread_pool/thread_pool_instance.h>
#include <fstream>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\json\single_include\nlohmann\json.hpp"


constexpr char kDataFolder[] = "data";
constexpr int kStoragePort = 8081;

int main() {
    // Set up the HTTP server for the storage service.
    httplib::Server svr;

    // Define the /store_chunk endpoint.
    svr.Post("/store_chunk", [](const httplib::Request& req, httplib::Response& res) {
        // Parse the multipart form data.
        if (!req.is_multipart_form_data()) {
            res.set_content("{\"error\": \"Expected multipart form data\"}", "application/json");
            res.status = 400;
            return;
        }

        // Check for the presence of "hash" and "chunk" fields using has_file().
        if (!req.has_file("hash") || !req.has_file("chunk")) {
            res.set_content("{\"error\": \"Missing hash or chunk in request\"}", "application/json");
            res.status = 400;
            return;
        }

        // Retrieve the hash and chunk data.
        const auto& hash_part = req.get_file_value("hash");
        const auto& chunk_part = req.get_file_value("chunk");

        if (hash_part.content.empty() || chunk_part.content.empty()) {
            res.set_content("{\"error\": \"Empty hash or chunk data\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string hash = hash_part.content;
        std::vector<uint8_t> chunk_data(chunk_part.content.begin(), chunk_part.content.end());

        // Ensure the data folder exists.
        std::filesystem::create_directory(kDataFolder);

        // Write the chunk to a file named "<hash>.chunk".
        std::string filename = std::string(kDataFolder) + "/" + hash + ".chunk";
        std::ofstream out_file(filename, std::ios::binary);
        if (!out_file) {
            res.set_content("{\"error\": \"Failed to write chunk to file\"}", "application/json");
            res.status = 500;
            return;
        }
        out_file.write(reinterpret_cast<const char*>(chunk_data.data()), chunk_data.size());
        out_file.close();

        // Placeholder for database storage of the hash.
        std::cout << "Received chunk with hash: " << hash << ", size: " << chunk_data.size() << " bytes" << std::endl;

        // Send success response.
        nlohmann::json response;
        response["status"] = "success";
        response["hash"] = hash;
        res.set_content(response.dump(), "application/json");
        res.status = 200;
    });

    // Start the server.
    std::cout << "Storage server listening on port " << kStoragePort << "..." << std::endl;
    svr.listen("0.0.0.0", kStoragePort);

    return 0;
}