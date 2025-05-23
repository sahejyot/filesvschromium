#include <base/task/thread_pool.h>
#include <C:/Users/Qikfox/Desktop/modular-chromium-threading/src/base/task/thread_pool/thread_pool_instance.h>
#include <third_party/boringssl/src/include/openssl/sha.h>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\cpp-httplib\httplib.h"
#include "windows_service.h"
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include "C:\Users\Qikfox\Desktop\modular-chromium-threading\src\third_party\json\single_include\nlohmann\json.hpp"


constexpr size_t kChunkSize = 1024 * 1024; // 1 MB
constexpr char kDataFolder[] = "data";
constexpr int kServerPort = 8080;

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

// Writes a chunk to a file named "<hash>.chunk" in the data folder.
void WriteChunkToFile(const std::string& hash, const uint8_t* data, size_t length) {
  std::filesystem::create_directory(kDataFolder);
  std::string filename = std::string(kDataFolder) + "/" + hash + ".chunk";
  std::ofstream out_file(filename, std::ios::binary);
  if (out_file) {
    out_file.write(reinterpret_cast<const char*>(data), length);
    out_file.close();
  } else {
    std::cerr << "Failed to write chunk to " << filename << std::endl;
  }
}

// Function to process a chunk: compute hash, write to file, and update chunk_hashes.
void ProcessChunk(std::vector<uint8_t> data, size_t size, std::vector<std::string>* chunk_hashes, std::mutex* hashes_mutex) {
  std::string hash = ComputeSHA256(data.data(), size);
  WriteChunkToFile(hash, data.data(), size);
  std::lock_guard<std::mutex> lock(*hashes_mutex);
  chunk_hashes->push_back(hash);
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
    std::vector<std::string> chunk_hashes;
    std::mutex hashes_mutex;

    // Process each chunk.
    for (size_t i = 0; i < num_chunks; ++i) {
      size_t offset = i * kChunkSize;
      size_t bytes_to_read = std::min(kChunkSize, file_size - offset);
      std::vector<uint8_t> chunk_data(
          reinterpret_cast<const uint8_t*>(file_data.data()) + offset,
          reinterpret_cast<const uint8_t*>(file_data.data()) + offset + bytes_to_read);

      // Post task to thread pool to compute hash and write chunk using a non-capturing lambda.
      base::ThreadPool::PostTask(
          FROM_HERE,
          base::BindOnce(
              // Non-capturing lambda
              [](std::vector<uint8_t> data, size_t size, std::vector<std::string>* chunk_hashes, std::mutex* hashes_mutex) {
                ProcessChunk(std::move(data), size, chunk_hashes, hashes_mutex);
              },
              std::move(chunk_data),
              bytes_to_read,
              &chunk_hashes,  // Pass chunk_hashes as a pointer
              &hashes_mutex  // Pass hashes_mutex as a pointer
          ));
    }

    // Wait for all tasks to complete.
    base::ThreadPoolInstance::Get()->Shutdown();

    // Prepare response.
    nlohmann::json response;
    response["status"] = "success";
    response["chunks_processed"] = num_chunks;
    response["chunk_hashes"] = chunk_hashes;

    res.set_content(response.dump(), "application/json");
    res.status = 200;
  });

  // Start the server.
  std::cout << "Server listening on port " << kServerPort << "..." << std::endl;
  svr.listen("0.0.0.0", kServerPort);

  return 0;
}