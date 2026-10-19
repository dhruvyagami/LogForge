#include "test_framework.hpp"
#include <indexer/tokenizer.hpp>
#include <vector>
#include <string>

using namespace indexer;

TEST_CASE(TokenizerBasicWords) {
    std::string text = "Hello World! This IS a test: 123.";
    auto tokens = Tokenizer::tokenize_string(text);

    std::vector<std::string> expected = {"hello", "world", "this", "is", "a", "test", "123"};
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        ASSERT_EQ(tokens[i], expected[i]);
    }
}

TEST_CASE(TokenizerPunctuationAndWhitespace) {
    std::string text = "---...   ### $$$ %%% @@@";
    auto tokens = Tokenizer::tokenize_string(text);
    ASSERT_TRUE(tokens.empty());
}

TEST_CASE(TokenizerChunkBoundarySplitting) {
    // Word "stream" is split: "str" in chunk 1, "eam" in chunk 2
    Tokenizer tok;
    std::vector<std::string> tokens;

    std::string chunk1 = "hello str";
    std::string chunk2 = "eam world";

    tok.process_chunk(chunk1.data(), chunk1.size(), false, [&tokens](std::string_view w) {
        tokens.emplace_back(w);
    });

    // At this point, "hello" should be emitted, and "str" held in carryover
    ASSERT_EQ(tokens.size(), 1u);
    ASSERT_EQ(tokens[0], "hello");
    ASSERT_TRUE(tok.has_carryover());

    tok.process_chunk(chunk2.data(), chunk2.size(), true, [&tokens](std::string_view w) {
        tokens.emplace_back(w);
    });

    // Now tokens should be: "hello", "stream", "world"
    ASSERT_EQ(tokens.size(), 3u);
    ASSERT_EQ(tokens[1], "stream");
    ASSERT_EQ(tokens[2], "world");
    ASSERT_FALSE(tok.has_carryover());
}

TEST_CASE(TokenizerSingleByteStreaming) {
    std::string text = "Alpha Beta Gamma";
    Tokenizer tok;
    std::vector<std::string> tokens;

    for (size_t i = 0; i < text.size(); ++i) {
        bool is_eof = (i == text.size() - 1);
        tok.process_chunk(&text[i], 1, is_eof, [&tokens](std::string_view w) {
            tokens.emplace_back(w);
        });
    }

    std::vector<std::string> expected = {"alpha", "beta", "gamma"};
    ASSERT_EQ(tokens.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        ASSERT_EQ(tokens[i], expected[i]);
    }
}

TEST_CASE(TokenizerEmptyInput) {
    Tokenizer tok;
    std::vector<std::string> tokens;
    tok.process_chunk(nullptr, 0, true, [&tokens](std::string_view w) {
        tokens.emplace_back(w);
    });
    ASSERT_TRUE(tokens.empty());
}
