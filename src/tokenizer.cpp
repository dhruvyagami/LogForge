#include <indexer/tokenizer.hpp>
#include <cctype>

namespace indexer {

void Tokenizer::reset() noexcept {
    carryover_.clear();
}

void Tokenizer::process_chunk(const char* data, 
                              size_t size, 
                              bool is_eof, 
                              const std::function<void(std::string_view)>& on_token) {
    if (size == 0) {
        if (is_eof && !carryover_.empty()) {
            on_token(carryover_);
            carryover_.clear();
        }
        return;
    }

    size_t idx = 0;

    // Handle carryover from previous chunk
    if (!carryover_.empty()) {
        if (is_alnum_ascii(data[0])) {
            // Continuation of the word that spanned the boundary
            while (idx < size && is_alnum_ascii(data[idx])) {
                carryover_.push_back(to_lower_ascii(data[idx]));
                ++idx;
            }

            if (idx < size) {
                // Word ended at delimiter data[idx]
                on_token(carryover_);
                carryover_.clear();
                ++idx; // move past delimiter
            } else {
                // Word still continues past the end of this chunk
                if (is_eof) {
                    on_token(carryover_);
                    carryover_.clear();
                }
                return;
            }
        } else {
            // First character is a delimiter, meaning carryover was already complete
            on_token(carryover_);
            carryover_.clear();
            ++idx; // move past delimiter
        }
    }

    // Tokenize main chunk body
    std::string current_token;
    current_token.reserve(64);

    while (idx < size) {
        // Skip leading non-alphanumeric delimiters
        while (idx < size && !is_alnum_ascii(data[idx])) {
            ++idx;
        }

        if (idx >= size) {
            break;
        }

        size_t token_start = idx;
        while (idx < size && is_alnum_ascii(data[idx])) {
            ++idx;
        }

        size_t token_len = idx - token_start;

        if (idx == size && !is_eof) {
            // Token touches the end of non-EOF chunk: must carry over
            carryover_.reserve(token_len);
            for (size_t i = token_start; i < idx; ++i) {
                carryover_.push_back(to_lower_ascii(data[i]));
            }
            return;
        }

        // Complete token found
        current_token.clear();
        current_token.reserve(token_len);
        for (size_t i = token_start; i < idx; ++i) {
            current_token.push_back(to_lower_ascii(data[i]));
        }
        on_token(current_token);

        ++idx; // move past delimiter
    }

    if (is_eof && !carryover_.empty()) {
        on_token(carryover_);
        carryover_.clear();
    }
}

std::vector<std::string> Tokenizer::tokenize_string(std::string_view text) {
    std::vector<std::string> tokens;
    Tokenizer tok;
    tok.process_chunk(text.data(), text.size(), true, [&tokens](std::string_view word) {
        tokens.emplace_back(word);
    });
    return tokens;
}

} // namespace indexer
