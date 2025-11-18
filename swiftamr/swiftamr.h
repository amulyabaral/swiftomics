#ifndef SWIFTAMR_H
#define SWIFTAMR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Configuration
#define KMER_SIZE 16
#define MAX_GENE_NAME 256
#define MAX_SEQUENCE_LENGTH (100 * 1024 * 1024) // 100MB max
#define HASH_TABLE_SIZE (1 << 24) // 16M entries

// Structures
typedef struct {
    uint32_t gene_id;
    uint32_t position;
} KmerHit;

typedef struct KmerEntry {
    uint64_t kmer;
    KmerHit* hits;
    uint32_t num_hits;
    uint32_t capacity;
    struct KmerEntry* next; // For hash collision chaining
} KmerEntry;

typedef struct {
    char name[MAX_GENE_NAME];
    char* sequence;
    uint32_t length;
} Gene;

typedef struct {
    KmerEntry** table;
    uint32_t table_size;
    Gene* genes;
    uint32_t num_genes;
    uint32_t genes_capacity;
} KmerIndex;

typedef struct {
    uint32_t gene_id;
    uint32_t score;        // Number of k-mer hits
    float coverage;        // Fraction of gene covered
    float identity;        // Estimated identity
} AlignmentResult;

typedef struct {
    char* read_name;
    AlignmentResult best_hit;
    uint32_t num_kmers_in_read;
} ReadAlignment;

// Function declarations

// Index building
KmerIndex* index_create(void);
void index_destroy(KmerIndex* index);
int index_add_gene(KmerIndex* index, const char* name, const char* sequence);
int index_build_from_fasta(KmerIndex* index, const char* fasta_data, size_t fasta_size);
void index_finalize(KmerIndex* index);

// K-mer operations
uint64_t kmer_encode(const char* seq);
int kmer_is_valid(const char* seq);
void kmer_add_to_index(KmerIndex* index, uint64_t kmer, uint32_t gene_id, uint32_t position);
KmerEntry* kmer_lookup(KmerIndex* index, uint64_t kmer);

// Alignment
ReadAlignment* align_read(KmerIndex* index, const char* read_name, const char* sequence, uint32_t seq_len);
void alignment_destroy(ReadAlignment* aln);
int align_fastq(KmerIndex* index, const char* fastq_data, size_t fastq_size,
                ReadAlignment*** results, uint32_t* num_results);

// Serialization (for pre-built index)
int index_save(KmerIndex* index, const char* filename);
KmerIndex* index_load(const char* filename);

// Utility
void print_alignment(ReadAlignment* aln, const KmerIndex* index);

#endif // SWIFTAMR_H
