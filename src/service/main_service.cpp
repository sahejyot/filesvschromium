#include <base/task/thread_pool.h>
#include <base/synchronization/waitable_event.h>
#include <third_party/boringssl/src/include/openssl/sha.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\cpp-httplib\httplib.h"
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\json\single_include\nlohmann\json.hpp"
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


constexpr size_t kChunkSize = 1024 * 1024; // 1 MB
constexpr int kServerPort = 8080;
constexpr char kStorageServiceUrl[] = "localhost:8081";

// Converts a byte array to a hex string.
std::string BytesToHex(const uint8_t* data, size_t length) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

// Computes the SHA-256 hash of a data buffer and returns it as a hex string.
std::string ComputeSHA256(const uint8_t* data, size_t length) {
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(data, length, hash);
    return BytesToHex(hash, SHA256_DIGEST_LENGTH);
}

// Sends a chunk and its hash to the storage service using multipart form data.
void SendChunkToStorage(
    std::vector<uint8_t> data,
    size_t size,
    size_t chunk_index,
    std::vector<std::string>* chunk_hashes,
    std::mutex* hashes_mutex,
    size_t num_chunks,
    std::atomic<size_t>* completed_tasks,
    base::WaitableEvent* all_tasks_done) {
    std::string hash = ComputeSHA256(data.data(), size);

    // Prepare the multipart form data.
    httplib::MultipartFormDataItems items = {
        {"hash", hash, "", "text/plain"},
        {"chunk", std::string(data.begin(), data.end()), "chunk.bin", "application/octet-stream"}
    };

    // Send the chunk to the storage service.
    httplib::Client cli(kStorageServiceUrl);
    cli.set_connection_timeout(5, 0); // 5 seconds timeout
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);

    auto res = cli.Post("/store_chunk", items);
    if (!res || res->status != 200) {
        std::cerr << "Failed to send chunk " << chunk_index << " to storage service: "
                  << (res ? std::to_string(res->status) : "connection error") << std::endl;
        hash.clear();
    } else {
        auto response_json = nlohmann::json::parse(res->body);
        if (response_json["status"] != "success" || response_json["hash"] != hash) {
            std::cerr << "Storage service failed to store chunk " << chunk_index << std::endl;
            hash.clear();
        }
    }

    // Store the hash in the correct position.
    {
        std::lock_guard<std::mutex> lock(*hashes_mutex);
        (*chunk_hashes)[chunk_index] = hash;
    }

    // Increment the completed tasks counter and signal if all tasks are done.
    if (completed_tasks->fetch_add(1, std::memory_order_relaxed) + 1 == num_chunks) {
        all_tasks_done->Signal();
    }
}

int main() {
    // Initialize the thread pool.
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("MainServer");

    // Set up the HTTP server.
    httplib::Server svr;

    // Define the /upload endpoint.
    svr.Post("/upload", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_file("file")) {
            res.set_content("{\"error\": \"No file uploaded\"}", "application/json");
            res.status = 400;
            return;
        }

        const auto& file = req.get_file_value("file");
        const auto& file_data = file.content;
        size_t file_size = file_data.size();

        if (file_size == 0) {
            res.set_content("{\"error\": \"Uploaded file is empty\"}", "application/json");
            res.status = 400;
            return;
        }

        // Calculate number of chunks.
        size_t num_chunks = (file_size + kChunkSize - 1) / kChunkSize;
        std::vector<std::string> chunk_hashes(num_chunks);
        std::mutex hashes_mutex;
        std::atomic<size_t> completed_tasks(0);
        base::WaitableEvent all_tasks_done;

        // Process each chunk using the thread pool.
        for (size_t i = 0; i < num_chunks; ++i) {
            size_t offset = i * kChunkSize;
            size_t bytes_to_read = std::min(kChunkSize, file_size - offset);
            std::vector<uint8_t> chunk_data(
                reinterpret_cast<const uint8_t*>(file_data.data()) + offset,
                reinterpret_cast<const uint8_t*>(file_data.data()) + offset + bytes_to_read);

            // Post task to thread pool to compute hash and send chunk to storage service.
            base::ThreadPool::PostTask(
                FROM_HERE,
                base::BindOnce(
                    &SendChunkToStorage,
                    std::move(chunk_data),
                    bytes_to_read,
                    i,
                    &chunk_hashes,
                    &hashes_mutex,
                    num_chunks,
                    &completed_tasks,
                    &all_tasks_done
                ));
        }

        // Wait for all tasks to complete.
        all_tasks_done.Wait();

        // Prepare response.
        nlohmann::json response;
        response["status"] = "success";
        response["chunks_processed"] = num_chunks;
        response["chunk_hashes"] = chunk_hashes;

        // Check for any failed chunks.
        for (size_t i = 0; i < num_chunks; ++i) {
            if (chunk_hashes[i].empty()) {
                response["status"] = "partial_success";
                response["error"] = "Some chunks failed to process. Check logs for details.";
                break;
            }
        }

        res.set_content(response.dump(), "application/json");
        res.status = response["status"] == "success" ? 200 : 207;
    });

    // Start the server.
    std::cout << "Main server listening on port " << kServerPort << "..." << std::endl;
    svr.listen("0.0.0.0", kServerPort);

    return 0;
}   
