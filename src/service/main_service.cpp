#define CPPHTTPLIB_OPENSSL_SUPPORT
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
#include <sqlite3.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include "database_initializer.h"


constexpr size_t kChunkSize = 1024 * 1024; // 4 MB
constexpr int kServerPort = 8080;
constexpr char kStorageServiceUrl[] = "https://localhost:8081";

// Paths to self-signed certificate and key (replace with actual paths)
constexpr char kMainServerCert[] = "C:/Users/Qikfox/Desktop/modular-chromium-threading/src/service/certs/main_service/server.crt";
constexpr char kMainServerKey[] = "C:/Users/Qikfox/Desktop/modular-chromium-threading/src/service/certs/main_service/server.key";

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

// Generates a CID by hashing the concatenation of the content hash, filename, and file type.
std::string GenerateCID(const std::string& content, const std::string& filename, const std::string& file_type) {
    std::string content_hash = ComputeSHA256(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    std::string combined = content_hash + filename + file_type;
    return ComputeSHA256(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
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
    base::WaitableEvent* all_tasks_done,
    httplib::Client* cli) {
    auto start = std::chrono::steady_clock::now();
    std::cout << "Chunk " << chunk_index << " started at "
              << std::chrono::duration<double>(start.time_since_epoch()).count() << " seconds" << std::endl;

    std::string hash = ComputeSHA256(data.data(), size);

    httplib::MultipartFormDataItems items = {
        {"hash", hash, "", "text/plain"},
        {"chunk", std::string(data.begin(), data.end()), "chunk.bin", "application/octet-stream"}
    };

    auto post_start = std::chrono::steady_clock::now();
    auto res = cli->Post("/store_chunk", items);
    auto post_end = std::chrono::steady_clock::now();
    std::cout << "HTTP Post for chunk " << chunk_index << " took "
              << std::chrono::duration<double>(post_end - post_start).count() << " seconds" << std::endl;

    if (!res) {
        std::cerr << "HTTP Post for chunk " << chunk_index << " failed: connection error" << std::endl;
        hash.clear();
    } else if (res->status != 200) {
        std::cerr << "HTTP Post for chunk " << chunk_index << " failed with status: " << res->status
                  << ", Response: " << res->body << std::endl;
        hash.clear();
    } else {
        auto response_json = nlohmann::json::parse(res->body);
        if (response_json["status"] != "success" || response_json["hash"] != hash) {
            std::cerr << "Storage service failed to store chunk " << chunk_index << std::endl;
            hash.clear();
        }
    }

    {
        std::lock_guard<std::mutex> lock(*hashes_mutex);
        (*chunk_hashes)[chunk_index] = hash;
    }

    if (completed_tasks->fetch_add(1, std::memory_order_relaxed) + 1 == num_chunks) {
        all_tasks_done->Signal();
    }

    auto end = std::chrono::steady_clock::now();
    std::cout << "Chunk " << chunk_index << " finished at "
              << std::chrono::duration<double>(end.time_since_epoch()).count() << " seconds" << std::endl;
    std::cout << "SendChunkToStorage for chunk " << chunk_index << " took "
              << std::chrono::duration<double>(end - start).count() << " seconds" << std::endl;
}

int main() {
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("MainServer");

    httplib::SSLServer svr(kMainServerCert, kMainServerKey);

    httplib::Client cli(kStorageServiceUrl);
    cli.enable_server_certificate_verification(false); // Handle self-signed certs
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);
    cli.set_keep_alive(true);

    svr.Post("/upload", [&cli](const httplib::Request& req, httplib::Response& res) {
        auto total_start = std::chrono::steady_clock::now();

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

        // Compute CID
        auto cid_start = std::chrono::steady_clock::now();
        std::string cid = GenerateCID(file_data, file.filename, file.content_type);
        auto cid_end = std::chrono::steady_clock::now();
        std::cout << "CID generation took "
                  << std::chrono::duration<double>(cid_end - cid_start).count() << " seconds" << std::endl;

        // Chunk and upload
        auto chunk_start = std::chrono::steady_clock::now();
        size_t num_chunks = (file_size + kChunkSize - 1) / kChunkSize;
        std::vector<std::string> chunk_hashes(num_chunks);
        std::mutex hashes_mutex;
        std::atomic<size_t> completed_tasks(0);
        base::WaitableEvent all_tasks_done;

        for (size_t i = 0; i < num_chunks; ++i) {
            size_t offset = i * kChunkSize;
            size_t bytes_to_read = std::min(kChunkSize, file_size - offset);
            std::vector<uint8_t> chunk_data(
                reinterpret_cast<const uint8_t*>(file_data.data()) + offset,
                reinterpret_cast<const uint8_t*>(file_data.data()) + offset + bytes_to_read);

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
                    &all_tasks_done,
                    &cli
                ));
        }

        all_tasks_done.Wait();
        auto chunk_end = std::chrono::steady_clock::now();
        std::cout << "Chunk processing took "
                  << std::chrono::duration<double>(chunk_end - chunk_start).count() << " seconds" << std::endl;

        nlohmann::json response;
        bool all_chunks_successful = true;
        for (size_t i = 0; i < num_chunks; ++i) {
            if (chunk_hashes[i].empty()) {
                all_chunks_successful = false;
                break;
            }
        }

        if (!all_chunks_successful) {
            response["status"] = "partial_success";
            response["error"] = "Some chunks failed to process. Check logs for details.";
            response["cid"] = cid;
            response["chunks_processed"] = num_chunks;
            response["chunk_hashes"] = chunk_hashes;
            res.set_content(response.dump(), "application/json");
            res.status = 207;
            return;
        }

        // Store metadata
        auto metadata_start = std::chrono::steady_clock::now();
        nlohmann::json metadata;
        metadata["cid"] = cid;
        metadata["filename"] = file.filename;
        metadata["file_type"] = file.content_type;
        metadata["file_size"] = file_size;
        metadata["chunk_hashes"] = chunk_hashes;

        auto metadata_post_start = std::chrono::steady_clock::now();
        auto metadata_res = cli.Post("/store_metadata", metadata.dump(), "application/json");
        auto metadata_post_end = std::chrono::steady_clock::now();
        std::cout << "HTTP Post for store_metadata took "
                  << std::chrono::duration<double>(metadata_post_end - metadata_post_start).count() << " seconds" << std::endl;

        auto metadata_end = std::chrono::steady_clock::now();
        std::cout << "Metadata storage took "
                  << std::chrono::duration<double>(metadata_end - metadata_start).count() << " seconds" << std::endl;

        if (!metadata_res || metadata_res->status != 200) {
            std::cerr << "Failed to store metadata: " << (metadata_res ? std::to_string(metadata_res->status) : "connection error")
                      << ", Response: " << (metadata_res ? metadata_res->body : "N/A") << std::endl;
            response["status"] = "error";
            response["error"] = "Failed to store metadata with storage service";
            response["cid"] = cid;
            res.set_content(response.dump(), "application/json");
            res.status = 500;
            return;
        }

        response["status"] = "success";
        response["cid"] = cid;
        response["chunks_processed"] = num_chunks;
        response["chunk_hashes"] = chunk_hashes;

        res.set_content(response.dump(), "application/json");
        res.status = 200;

        auto total_end = std::chrono::steady_clock::now();
        std::cout << "Total upload took "
                  << std::chrono::duration<double>(total_end - total_start).count() << " seconds" << std::endl;
    });

    std::cout << "Main server listening on port " << kServerPort << "..." << std::endl;
    svr.listen("0.0.0.0", kServerPort);

    return 0;
}