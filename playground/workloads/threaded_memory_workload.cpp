#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

constexpr int kBufferCountPerThread = 16;
constexpr size_t kBufferSizeBytes = 2 * 1024 * 1024;

bool parseDouble(const char* pRawValue, double& rOutput) {
    if (pRawValue == nullptr) {
        return false;
    }
    char* pEnd = nullptr;
    double value = std::strtod(pRawValue, &pEnd);
    if ((pEnd == nullptr) || (*pEnd != '\0') || (value < 0.0)) {
        return false;
    }
    rOutput = value;
    return true;
}

bool parseInt(const char* pRawValue, int& rOutput) {
    if (pRawValue == nullptr) {
        return false;
    }
    char* pEnd = nullptr;
    long value = std::strtol(pRawValue, &pEnd, 10);
    if ((pEnd == nullptr) || (*pEnd != '\0') || (value <= 0)) {
        return false;
    }
    rOutput = static_cast<int>(value);
    return true;
}

void runWorker(int threadIdx, double computeSeconds, uint64_t& rChecksumOutput) {
    std::vector<std::vector<uint8_t>> buffers;
    buffers.reserve(kBufferCountPerThread);
    for (int bufferIdx = 0; bufferIdx < kBufferCountPerThread; ++bufferIdx) {
        buffers.push_back(std::vector<uint8_t>(kBufferSizeBytes, 0));
    }

    std::mt19937 rng(12345U + static_cast<uint32_t>(threadIdx));
    const auto endTime = std::chrono::steady_clock::now() + std::chrono::duration<double>(computeSeconds);

    uint64_t checksum = 0;
    while (std::chrono::steady_clock::now() < endTime) {
        std::shuffle(buffers.begin(), buffers.end(), rng);
        for (std::vector<uint8_t>& rBuffer : buffers) {
            for (size_t idx = 0; idx < rBuffer.size(); idx += 64) {
                uint8_t value = static_cast<uint8_t>((rBuffer[idx] + 1U) & 0xFFU);
                rBuffer[idx] = value;
                checksum += value;
            }
        }
    }

    rChecksumOutput = checksum;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "Usage: threaded_memory_workload <sleep_seconds> <compute_seconds> <thread_count>\n");
        return 1;
    }

    double sleepSeconds = 0.0;
    double computeSeconds = 0.0;
    int threadCount = 0;
    if (!parseDouble(argv[1], sleepSeconds) || !parseDouble(argv[2], computeSeconds) || !parseInt(argv[3], threadCount)) {
        std::fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    std::printf("Workload PID: %d\n", static_cast<int>(getpid()));
    std::printf("Threaded workload sleeping %.3f seconds before memory computation...\n", sleepSeconds);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::duration<double>(sleepSeconds));

    std::printf("Threaded workload computation started with %d threads.\n", threadCount);
    std::fflush(stdout);

    std::vector<std::thread> workers;
    std::vector<uint64_t> checksums(static_cast<size_t>(threadCount), 0);
    workers.reserve(static_cast<size_t>(threadCount));
    for (int threadIdx = 0; threadIdx < threadCount; ++threadIdx) {
        workers.emplace_back(runWorker, threadIdx, computeSeconds, std::ref(checksums[threadIdx]));
    }
    for (std::thread& rWorker : workers) {
        rWorker.join();
    }

    uint64_t totalChecksum = 0;
    for (uint64_t checksum : checksums) {
        totalChecksum += checksum;
    }

    std::printf("Threaded workload done. checksum=%llu\n", static_cast<unsigned long long>(totalChecksum));
    std::fflush(stdout);
    return 0;
}
