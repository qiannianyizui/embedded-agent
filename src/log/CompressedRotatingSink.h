// CompressedRotatingSink — spdlog custom sink with zstd streaming compression
// and size-based rotation with total-size cleanup.
//
// Active file:   {log_dir}/agent.log.zst
// Sealed files:  {log_dir}/agent_log_YYYYMMDD_HHMMSS.zst
//
// When the active file exceeds max_file_bytes, it is sealed (renamed with
// timestamp) and a new active file is opened. If the total size of all
// sealed files exceeds max_total_bytes, the oldest are deleted.
#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <zstd.h>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <string>

namespace ea::log {

class CompressedRotatingSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    CompressedRotatingSink(const std::string& log_dir,
                           int max_file_bytes,
                           int max_total_bytes)
        : log_dir_(log_dir)
        , max_file_bytes_(max_file_bytes)
        , max_total_bytes_(max_total_bytes)
        , active_path_(log_dir + "/agent.log.zst")
    {
        std::filesystem::create_directories(log_dir);
        open_active();
    }

    ~CompressedRotatingSink() override {
        close_active();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (!file_) return;

        // Format the message
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);

        // Feed into zstd compressor
        ZSTD_inBuffer input = {formatted.data(), formatted.size(), 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {out_buf_, sizeof(out_buf_), 0};
            size_t ret = ZSTD_compressStream2(cctx_, &output, &input, ZSTD_e_continue);
            if (ZSTD_isError(ret)) return;
            if (output.pos > 0) {
                std::fwrite(out_buf_, 1, output.pos, file_);
                written_bytes_ += output.pos;
            }
        }

        // Check if rotation needed
        if (written_bytes_ >= static_cast<size_t>(max_file_bytes_)) {
            seal_and_rotate();
        }
    }

    void flush_() override {
        if (!file_) return;

        // Flush zstd stream
        ZSTD_inBuffer input = {nullptr, 0, 0};
        while (true) {
            ZSTD_outBuffer output = {out_buf_, sizeof(out_buf_), 0};
            size_t ret = ZSTD_compressStream2(cctx_, &output, &input, ZSTD_e_flush);
            if (output.pos > 0) {
                std::fwrite(out_buf_, 1, output.pos, file_);
            }
            if (ret == 0) break;
            if (ZSTD_isError(ret)) break;
        }
        std::fflush(file_);
    }

private:
    static constexpr size_t OUT_BUF_SIZE = 64 * 1024;
    static constexpr int ZSTD_LEVEL = 3;  // fast + decent ratio

    std::string log_dir_;
    int max_file_bytes_;
    int max_total_bytes_;
    std::string active_path_;

    FILE* file_ = nullptr;
    ZSTD_CCtx* cctx_ = nullptr;
    char out_buf_[OUT_BUF_SIZE];
    size_t written_bytes_ = 0;

    void open_active() {
        file_ = std::fopen(active_path_.c_str(), "ab");  // append binary
        if (!file_) return;

        cctx_ = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, ZSTD_LEVEL);

        // Count existing bytes for append mode
        written_bytes_ = std::filesystem::exists(active_path_)
            ? std::filesystem::file_size(active_path_)
            : 0;
    }

    void close_active() {
        if (cctx_) {
            // End the zstd frame
            ZSTD_inBuffer input = {nullptr, 0, 0};
            while (true) {
                ZSTD_outBuffer output = {out_buf_, sizeof(out_buf_), 0};
                size_t ret = ZSTD_compressStream2(cctx_, &output, &input, ZSTD_e_end);
                if (output.pos > 0 && file_) {
                    std::fwrite(out_buf_, 1, output.pos, file_);
                }
                if (ret == 0) break;
                if (ZSTD_isError(ret)) break;
            }
            ZSTD_freeCCtx(cctx_);
            cctx_ = nullptr;
        }
        if (file_) {
            std::fflush(file_);
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    void seal_and_rotate() {
        close_active();

        // Rename active → sealed with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf {};
        localtime_r(&time_t_now, &tm_buf);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm_buf);

        std::string sealed_path = log_dir_ + "/agent_log_" + ts + ".zst";
        std::filesystem::rename(active_path_, sealed_path);

        // Cleanup old sealed files if total size exceeds limit
        cleanup_sealed();

        // Open new active file
        written_bytes_ = 0;
        open_active();
    }

    void cleanup_sealed() {
        namespace fs = std::filesystem;

        // Collect all sealed files with their sizes and modification times
        struct SealedFile {
            fs::path path;
            size_t size;
            fs::file_time_type mtime;
        };
        std::vector<SealedFile> sealed;

        size_t total = 0;
        for (const auto& entry : fs::directory_iterator(log_dir_)) {
            auto name = entry.path().filename().string();
            if (name.size() > 15 &&
                name.compare(0, 10, "agent_log_") == 0 &&
                name.compare(name.size() - 4, 4, ".zst") == 0) {
                try {
                    sealed.push_back({entry.path(),
                                      fs::file_size(entry.path()),
                                      fs::last_write_time(entry.path())});
                    total += sealed.back().size;
                } catch (...) {}
            }
        }

        if (total <= static_cast<size_t>(max_total_bytes_)) return;

        // Sort oldest first
        std::sort(sealed.begin(), sealed.end(),
                  [](const SealedFile& a, const SealedFile& b) { return a.mtime < b.mtime; });

        // Delete oldest until under limit
        for (const auto& f : sealed) {
            if (total <= static_cast<size_t>(max_total_bytes_)) break;
            try {
                fs::remove(f.path);
                total -= f.size;
            } catch (...) {}
        }
    }
};

}  // namespace ea::log
