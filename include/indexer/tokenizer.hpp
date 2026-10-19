#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace indexer {

/**
 * @brief High-performance streaming tokenizer for alphanumeric text.
 * 
 * Extracts words defined as contiguous alphanumeric sequences ([a-zA-Z0-9]+),
 * lowercasing all uppercase ASCII characters. Non-alphanumeric characters serve
 * as word delimiters.
 * 
 * Specifically designed to handle chunked/streaming I/O: if a word is split across
 * chunk boundaries (e.g., buffer 1 ends with "str" and buffer 2 starts with "eam"),
 * the Tokenizer preserves the trailing prefix in an internal carryover buffer until
 * the next chunk arrives or EOF is encountered.
 */
class Tokenizer {
public:
    Tokenizer() = default;
    ~Tokenizer() = default;

    // Disallow copy to avoid unexpected duplicated carryover state; allow move
    Tokenizer(const Tokenizer&) = delete;
    Tokenizer& operator=(const Tokenizer&) = delete;
    Tokenizer(Tokenizer&&) noexcept = default;
    Tokenizer& operator=(Tokenizer&&) noexcept = default;

    /**
     * @brief Process an incoming memory chunk from a file or stream.
     * 
     * @param data Pointer to the chunk bytes (can be nullptr if size == 0).
     * @param size Number of bytes in the chunk.
     * @param is_eof True if this is the final chunk of the stream.
     * @param on_token Callback invoked for each complete lowercased word.
     */
    void process_chunk(const char* data, 
                       size_t size, 
                       bool is_eof, 
                       const std::function<void(std::string_view)>& on_token);

    /**
     * @brief Reset tokenizer state, clearing any carryover buffers.
     */
    void reset() noexcept;

    /**
     * @brief Check whether carryover buffer currently holds partial word data.
     */
    bool has_carryover() const noexcept { return !carryover_.empty(); }

    /**
     * @brief Convenience helper to tokenize a complete string in-memory.
     * 
     * @param text Input string view.
     * @return std::vector<std::string> List of extracted lowercase tokens.
     */
    static std::vector<std::string> tokenize_string(std::string_view text);

    /**
     * @brief Fast ASCII lowercase conversion.
     */
    static inline char to_lower_ascii(char c) noexcept {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c + ('a' - 'A'));
        }
        return c;
    }

    /**
     * @brief Fast ASCII alphanumeric check.
     */
    static inline bool is_alnum_ascii(char c) noexcept {
        return (c >= 'a' && c <= 'z') || 
               (c >= 'A' && c <= 'Z') || 
               (c >= '0' && c <= '9');
    }

private:
    std::string carryover_;
};

} // namespace indexer
