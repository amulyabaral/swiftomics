#include "swiftamr.h"
#include <emscripten.h>

// Global index
static KmerIndex* global_index = NULL;

// WASM-exported function: Initialize index from FASTA data
EMSCRIPTEN_KEEPALIVE
int swiftamr_build_index(const char* fasta_data, size_t fasta_size) {
    if (global_index) {
        index_destroy(global_index);
    }

    global_index = index_create();
    if (!global_index) {
        printf("ERROR: Failed to create index\n");
        return -1;
    }

    printf("Building k-mer index from FASTA...\n");
    int genes_added = index_build_from_fasta(global_index, fasta_data, fasta_size);

    if (genes_added < 0) {
        printf("ERROR: Failed to build index\n");
        index_destroy(global_index);
        global_index = NULL;
        return -1;
    }

    printf("Index built successfully: %d genes, %u total genes in index\n",
           genes_added, global_index->num_genes);

    return genes_added;
}

// WASM-exported function: Align FASTQ reads
EMSCRIPTEN_KEEPALIVE
char* swiftamr_align_fastq(const char* fastq_data, size_t fastq_size) {
    if (!global_index) {
        printf("ERROR: Index not initialized\n");
        return strdup("ERROR: Index not initialized");
    }

    printf("Aligning reads from FASTQ...\n");

    ReadAlignment** results = NULL;
    uint32_t num_results = 0;

    int ret = align_fastq(global_index, fastq_data, fastq_size, &results, &num_results);

    if (ret < 0) {
        printf("ERROR: Alignment failed\n");
        return strdup("ERROR: Alignment failed");
    }

    printf("Aligned %u reads\n", num_results);

    // Generate TSV output
    size_t buffer_size = num_results * 512; // Estimate
    char* output = (char*)malloc(buffer_size);
    size_t pos = 0;

    // Header
    pos += snprintf(output + pos, buffer_size - pos,
                    "read_name\tgene\tscore\tcoverage\tidentity\n");

    // Results
    for (uint32_t i = 0; i < num_results; i++) {
        ReadAlignment* aln = results[i];

        const char* gene_name = "No_hit";
        if (aln->best_hit.gene_id != UINT32_MAX) {
            gene_name = global_index->genes[aln->best_hit.gene_id].name;
        }

        pos += snprintf(output + pos, buffer_size - pos,
                       "%s\t%s\t%u\t%.4f\t%.4f\n",
                       aln->read_name,
                       gene_name,
                       aln->best_hit.score,
                       aln->best_hit.coverage,
                       aln->best_hit.identity);

        alignment_destroy(aln);
    }

    if (results) free(results);

    return output;
}

// WASM-exported function: Get index stats
EMSCRIPTEN_KEEPALIVE
char* swiftamr_get_stats() {
    if (!global_index) {
        return strdup("No index loaded");
    }

    char* stats = (char*)malloc(1024);
    snprintf(stats, 1024,
             "Index Statistics:\n"
             "  Number of genes: %u\n"
             "  K-mer size: %d\n"
             "  Hash table size: %u\n",
             global_index->num_genes,
             KMER_SIZE,
             global_index->table_size);

    return stats;
}

// WASM-exported function: Free resources
EMSCRIPTEN_KEEPALIVE
void swiftamr_cleanup() {
    if (global_index) {
        index_destroy(global_index);
        global_index = NULL;
    }
}

// For testing in native environment
#ifndef __EMSCRIPTEN__
int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <database.fasta> <reads.fastq>\n", argv[0]);
        return 1;
    }

    // Load FASTA
    FILE* fasta_file = fopen(argv[1], "r");
    if (!fasta_file) {
        printf("ERROR: Cannot open FASTA file\n");
        return 1;
    }

    fseek(fasta_file, 0, SEEK_END);
    size_t fasta_size = ftell(fasta_file);
    fseek(fasta_file, 0, SEEK_SET);

    char* fasta_data = (char*)malloc(fasta_size + 1);
    fread(fasta_data, 1, fasta_size, fasta_file);
    fasta_data[fasta_size] = '\0';
    fclose(fasta_file);

    // Build index
    int ret = swiftamr_build_index(fasta_data, fasta_size);
    free(fasta_data);

    if (ret < 0) return 1;

    // Load FASTQ
    FILE* fastq_file = fopen(argv[2], "r");
    if (!fastq_file) {
        printf("ERROR: Cannot open FASTQ file\n");
        return 1;
    }

    fseek(fastq_file, 0, SEEK_END);
    size_t fastq_size = ftell(fastq_file);
    fseek(fastq_file, 0, SEEK_SET);

    char* fastq_data = (char*)malloc(fastq_size + 1);
    fread(fastq_data, 1, fastq_size, fastq_file);
    fastq_data[fastq_size] = '\0';
    fclose(fastq_file);

    // Align
    char* results = swiftamr_align_fastq(fastq_data, fastq_size);
    free(fastq_data);

    printf("\n%s\n", results);
    free(results);

    swiftamr_cleanup();

    return 0;
}
#endif
