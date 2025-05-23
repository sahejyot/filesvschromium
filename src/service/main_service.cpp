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

#include "windows_service.h"
#include <base/logging.h>
#include <base/threading/platform_thread.h>

constexpr size_t kChunkSize = 1024 * 1024; // 1 MB
constexpr char kDataFolder[] = "data";

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

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
    return 1;
  }

  // Open the input file.
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Failed to open file: " << argv[1] << std::endl;
    return 1;
  }

  // Get file size.
  std::streamsize file_size = file.tellg();
  if (file_size < 0) {
    std::cerr << "Failed to get file size." << std::endl;
    return 1;
  }
  file.seekg(0, std::ios::beg);

  // Initialize the thread pool.
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("MainServer");

  // Calculate number of chunks.
  size_t num_chunks = (file_size + kChunkSize - 1) / kChunkSize;
  std::vector<uint8_t> buffer(kChunkSize);

  // Process each chunk.
  for (size_t i = 0; i < num_chunks; ++i) {
    // Read chunk.
    size_t bytes_to_read = std::min(kChunkSize, static_cast<size_t>(file_size - file.tellg()));
    file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);
    size_t bytes_read = file.gcount();
    if (bytes_read == 0 && !file.eof()) {
      std::cerr << "Failed to read chunk " << i << std::endl;
      continue;
    }

    // Post task to thread pool to compute hash and write chunk.
    base::ThreadPool::PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::vector<uint8_t> data, size_t size) {
              std::string hash = ComputeSHA256(data.data(), size);
              WriteChunkToFile(hash, data.data(), size);
            },
            std::vector<uint8_t>(buffer.begin(), buffer.begin() + bytes_read),
            bytes_read));
  }

  file.close();

  // Shut down the thread pool and wait for tasks to complete.
  base::ThreadPoolInstance::Get()->Shutdown();

  std::cout << "Processed " << num_chunks << " chunks. Chunks written to 'data' folder." << std::endl;
  return 0;
}