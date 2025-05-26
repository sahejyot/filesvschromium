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

std::string BytesToHex(const uint8_t* data, size_t length) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return oss.str();
}

std::string ComputeSHA256(const uint8_t* data, size_t length) {
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256(data, length, hash);
    return BytesToHex(hash, SHA256_DIGEST_LENGTH);
}

std::string GenerateCID(const std::string& content, const std::string& filename, const std::string& file_type) {
    std::string content_hash = ComputeSHA256(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    std::string combined = content_hash + filename + file_type;
    return ComputeSHA256(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
}

bool CheckStorageServerAvailability(httplib::Client* cli) {
    const int max_attempts = 3;
    const int retry_delay_ms = 1000; // 1 second delay between retries

    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        std::cout << "Attempting to check storage server availability (attempt " << attempt << " of " << max_attempts << ")..." << std::endl;
        httplib::Result res = cli->Get("/health");
        if (!res) {
            std::cerr << "Storage server health check failed: connection error (" << res.error() << ")" << std::endl;
        } else if (res->status != 200) {
            std::cerr << "Storage server health check failed with status: " << res->status << ", Response: " << res->body << std::endl;
        } else {
            std::cout << "Storage server is available" << std::endl;
            return true;
        }

        if (attempt < max_attempts) {
            std::cout << "Retrying in " << retry_delay_ms << " ms..." << std::endl;
            base::PlatformThread::Sleep(base::Milliseconds(retry_delay_ms));
        }
    }

    std::cerr << "All attempts to connect to storage server failed" << std::endl;
    return false;
}

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
        std::cerr << "HTTP Post for chunk " << chunk_index << " failed: connection error (" << res.error() << ")" << std::endl;
        hash.clear();
    } else if (res->status != 200) {
        std::cerr << "HTTP Post for chunk " << chunk_index << " failed with status: " << res->status
                  << ", Response: " << res->body << std::endl;
        hash.clear();
    } else {
        try {
            auto response_json = nlohmann::json::parse(res->body);
            if (response_json["status"] != "success" || response_json["hash"] != hash) {
                std::cerr << "Storage service failed to store chunk " << chunk_index << ": " << res->body << std::endl;
                hash.clear();
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to parse storage service response for chunk " << chunk_index << ": " << e.what() << std::endl;
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
    cli.enable_server_certificate_verification(false);
    cli.set_connection_timeout(10, 0); // 10 seconds
    cli.set_read_timeout(10, 0); // 10 seconds
    cli.set_write_timeout(10, 0); // 10 seconds
    cli.set_keep_alive(true);

    if (!CheckStorageServerAvailability(&cli)) {
        std::cerr << "Cannot start main server: storage server is unavailable" << std::endl;
        return 1;
    }

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

        auto cid_start = std::chrono::steady_clock::now();
        std::string cid = GenerateCID(file_data, file.filename, file.content_type);
        auto cid_end = std::chrono::steady_clock::now();
        std::cout << "CID generation took "
                  << std::chrono::duration<double>(cid_end - cid_start).count() << " seconds" << std::endl;

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

    svr.Put("/update", [&cli](const httplib::Request& req, httplib::Response& res) {
        auto total_start = std::chrono::steady_clock::now();

        if (!req.has_param("cid")) {
            res.set_content("{\"error\": \"Missing cid parameter\"}", "application/json");
            res.status = 400;
            return;
        }

        if (!req.has_file("file")) {
            res.set_content("{\"error\": \"No file uploaded\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string old_cid = req.get_param_value("cid");

        const auto& file = req.get_file_value("file");
        const auto& file_data = file.content;
        size_t file_size = file_data.size();

        if (file_size == 0) {
            res.set_content("{\"error\": \"Uploaded file is empty\"}", "application/json");
            res.status = 400;
            return;
        }

        auto cid_start = std::chrono::steady_clock::now();
        std::string new_cid = GenerateCID(file_data, file.filename, file.content_type);
        auto cid_end = std::chrono::steady_clock::now();
        std::cout << "CID generation took "
                  << std::chrono::duration<double>(cid_end - cid_start).count() << " seconds" << std::endl;

        nlohmann::json response;
        if (new_cid == old_cid) {
            response["status"] = "success";
            response["message"] = "No changes detected";
            response["cid"] = old_cid;
            res.set_content(response.dump(), "application/json");
            res.status = 200;

            auto total_end = std::chrono::steady_clock::now();
            std::cout << "Total update (no changes) took "
                      << std::chrono::duration<double>(total_end - total_start).count() << " seconds" << std::endl;
            return;
        }

        auto delete_start = std::chrono::steady_clock::now();
        auto delete_res = cli.Delete("/delete?cid=" + old_cid);
        auto delete_end = std::chrono::steady_clock::now();
        std::cout << "HTTP Delete for old CID took "
                  << std::chrono::duration<double>(delete_end - delete_start).count() << " seconds" << std::endl;

        if (!delete_res || delete_res->status != 200) {
            std::cerr << "Failed to delete old CID: " << (delete_res ? std::to_string(delete_res->status) : "connection error")
                      << ", Response: " << (delete_res ? delete_res->body : "N/A") << std::endl;
            response["status"] = "error";
            response["error"] = "Failed to delete old file";
            response["cid"] = old_cid;
            res.set_content(response.dump(), "application/json");
            res.status = 500;
            return;
        }

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
            response["cid"] = new_cid;
            response["chunks_processed"] = num_chunks;
            response["chunk_hashes"] = chunk_hashes;
            res.set_content(response.dump(), "application/json");
            res.status = 207;
            return;
        }

        auto metadata_start = std::chrono::steady_clock::now();
        nlohmann::json metadata;
        metadata["cid"] = new_cid;
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
            response["cid"] = new_cid;
            res.set_content(response.dump(), "application/json");
            res.status = 500;
            return;
        }

        response["status"] = "success";
        response["message"] = "File updated successfully";
        response["old_cid"] = old_cid;
        response["new_cid"] = new_cid;
        response["chunks_processed"] = num_chunks;
        response["chunk_hashes"] = chunk_hashes;

        res.set_content(response.dump(), "application/json");
        res.status = 200;

        auto total_end = std::chrono::steady_clock::now();
        std::cout << "Total update took "
                  << std::chrono::duration<double>(total_end - total_start).count() << " seconds" << std::endl;
    });
    svr.Get("/get", [&cli](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("cid")) {
            res.set_content("{\"error\": \"Missing cid parameter\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string cid = req.get_param_value("cid");

        auto get_start = std::chrono::steady_clock::now();
        auto get_res = cli.Get("/get?cid=" + cid);
        auto get_end = std::chrono::steady_clock::now();
        std::cout << "HTTP Get for CID " << cid << " took "
                  << std::chrono::duration<double>(get_end - get_start).count() << " seconds" << std::endl;

        if (!get_res) {
            std::cerr << "Failed to retrieve CID " << cid << ": connection error (" << get_res.error() << ")" << std::endl;
            res.set_content("{\"error\": \"Failed to connect to storage server\"}", "application/json");
            res.status = 500;
            return;
        }

        if (get_res->status != 200) {
            std::cerr << "Failed to retrieve CID " << cid << ": storage server returned status " << get_res->status
                      << ", Response: " << get_res->body << std::endl;
            res.set_content(get_res->body, "application/json");
            res.status = get_res->status;
            return;
        }

        // Forward the content and headers from the storage server
        res.set_content(get_res->body, get_res->headers.find("Content-Type")->second);
        auto it = get_res->headers.find("Content-Disposition");
        if (it != get_res->headers.end()) {
            res.set_header("Content-Disposition", it->second);
        }
        res.status = 200;
    });
    svr.Delete("/delete", [&cli](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("cid")) {
            res.set_content("{\"error\": \"Missing cid parameter\"}", "application/json");
            res.status = 400;
            return;
        }

        std::string cid = req.get_param_value("cid");

        auto delete_start = std::chrono::steady_clock::now();
        auto delete_res = cli.Delete("/delete?cid=" + cid);
        auto delete_end = std::chrono::steady_clock::now();
        std::cout << "HTTP Delete for CID " << cid << " took "
                  << std::chrono::duration<double>(delete_end - delete_start).count() << " seconds" << std::endl;

        if (!delete_res) {
            std::cerr << "Failed to delete CID " << cid << ": connection error (" << delete_res.error() << ")" << std::endl;
            res.set_content("{\"error\": \"Failed to connect to storage server\"}", "application/json");
            res.status = 500;
            return;
        }

        if (delete_res->status != 200) {
            std::cerr << "Failed to delete CID " << cid << ": storage server returned status " << delete_res->status
                      << ", Response: " << delete_res->body << std::endl;
            res.set_content(delete_res->body, "application/json");
            res.status = delete_res->status;
            return;
        }

        res.set_content("{\"status\": \"success\", \"cid\": \"" + cid + "\"}", "application/json");
        res.status = 200;
    });

    std::cout << "Main server listening on port " << kServerPort << "..." << std::endl;
    svr.listen("0.0.0.0", kServerPort);

    return 0;
}