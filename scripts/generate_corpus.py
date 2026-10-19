#!/usr/bin/env python3
"""
Synthetic Corpus Generator for LogForge Benchmark
Generates a configurable size corpus of text files following a Zipfian/power-law word distribution.
"""

import sys
import os
import argparse
import random
import math

VOCABULARY = [
    "the", "be", "to", "of", "and", "a", "in", "that", "have", "i",
    "it", "for", "not", "on", "with", "he", "as", "you", "do", "at",
    "this", "but", "his", "by", "from", "they", "we", "say", "her", "she",
    "or", "an", "will", "my", "one", "all", "would", "there", "their", "what",
    "so", "up", "out", "if", "about", "who", "get", "which", "go", "me",
    "indexer", "pipeline", "concurrency", "posix", "mmap", "syscall", "kernel",
    "throughput", "latency", "benchmark", "storage", "durable", "fsync", "pipe"
]

def generate_zipf_weights(n, alpha=1.1):
    weights = [1.0 / (math.pow(i + 1, alpha)) for i in range(n)]
    total = sum(weights)
    return [w / total for w in weights]

def generate_corpus(output_dir, target_size_bytes, num_files):
    os.makedirs(output_dir, exist_ok=True)
    weights = generate_zipf_weights(len(VOCABULARY))
    bytes_per_file = target_size_bytes // num_files

    print(f"Generating {target_size_bytes / (1024*1024):.1f} MiB corpus across {num_files} files in '{output_dir}'...")

    for file_idx in range(num_files):
        file_path = os.path.join(output_dir, f"shard_{file_idx:04d}.txt")
        written = 0
        with open(file_path, "w", encoding="utf-8") as f:
            chunk = []
            chunk_len = 0
            while written < bytes_per_file:
                # Sample words according to Zipf distribution
                words = random.choices(VOCABULARY, weights=weights, k=500)
                line = " ".join(words) + "\n"
                chunk.append(line)
                chunk_len += len(line)
                if chunk_len >= 64 * 1024:
                    text = "".join(chunk)
                    f.write(text)
                    written += len(text)
                    chunk = []
                    chunk_len = 0
            if chunk:
                text = "".join(chunk)
                f.write(text)
                written += len(text)

    print("Corpus generation complete.")

def main():
    parser = argparse.ArgumentParser(description="Synthetic text corpus generator for LogForge")
    parser.add_argument("--dir", default="./bench_corpus", help="Output directory")
    parser.add_argument("--size-mib", type=int, default=1024, help="Target total corpus size in MiB (default: 1024 MiB = 1 GiB)")
    parser.add_argument("--files", type=int, default=64, help="Number of files to distribute corpus over (default: 64)")
    args = parser.parse_args()

    target_bytes = args.size_mib * 1024 * 1024
    generate_corpus(args.dir, target_bytes, args.files)

if __name__ == "__main__":
    main()
