#include "swiftamr.h"
#include <ctype.h>
#include <math.h>

// Nucleotide encoding: A=0, C=1, G=2, T=3
static inline int nt_to_int(char c) {
    switch (toupper(c)) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default: return -1;
    }
}

// Check if k-mer contains only valid nucleotides
int kmer_is_valid(const char* seq) {
    for (int i = 0; i < KMER_SIZE; i++) {
        if (nt_to_int(seq[i]) < 0) return 0;
    }
    return 1;
}

// Encode k-mer as 64-bit integer (2 bits per nucleotide)
uint64_t kmer_encode(const char* seq) {
    uint64_t kmer = 0;
    for (int i = 0; i < KMER_SIZE; i++) {
        int nt = nt_to_int(seq[i]);
        if (nt < 0) return UINT64_MAX; // Invalid k-mer
        kmer = (kmer << 2) | nt;
    }
    return kmer;
}

// Create new k-mer index
KmerIndex* index_create(void) {
    KmerIndex* index = (KmerIndex*)calloc(1, sizeof(KmerIndex));
    if (!index) return NULL;

    index->table_size = HASH_TABLE_SIZE;
    index->table = (KmerEntry**)calloc(HASH_TABLE_SIZE, sizeof(KmerEntry*));
    if (!index->table) {
        free(index);
        return NULL;
    }

    index->genes_capacity = 1024;
    index->genes = (Gene*)calloc(index->genes_capacity, sizeof(Gene));
    if (!index->genes) {
        free(index->table);
        free(index);
        return NULL;
    }

    index->num_genes = 0;
    return index;
}

// Destroy k-mer index and free memory
void index_destroy(KmerIndex* index) {
    if (!index) return;

    // Free hash table
    if (index->table) {
        for (uint32_t i = 0; i < index->table_size; i++) {
            KmerEntry* entry = index->table[i];
            while (entry) {
                KmerEntry* next = entry->next;
                if (entry->hits) free(entry->hits);
                free(entry);
                entry = next;
            }
        }
        free(index->table);
    }

    // Free genes
    if (index->genes) {
        for (uint32_t i = 0; i < index->num_genes; i++) {
            if (index->genes[i].sequence) {
                free(index->genes[i].sequence);
            }
        }
        free(index->genes);
    }

    free(index);
}

// Add k-mer to index
void kmer_add_to_index(KmerIndex* index, uint64_t kmer, uint32_t gene_id, uint32_t position) {
    uint32_t hash = kmer % index->table_size;

    // Find or create entry
    KmerEntry* entry = index->table[hash];
    KmerEntry* prev = NULL;

    while (entry && entry->kmer != kmer) {
        prev = entry;
        entry = entry->next;
    }

    if (!entry) {
        // Create new entry
        entry = (KmerEntry*)calloc(1, sizeof(KmerEntry));
        entry->kmer = kmer;
        entry->capacity = 4;
        entry->hits = (KmerHit*)malloc(entry->capacity * sizeof(KmerHit));
        entry->num_hits = 0;
        entry->next = NULL;

        if (prev) {
            prev->next = entry;
        } else {
            index->table[hash] = entry;
        }
    }

    // Add hit to entry
    if (entry->num_hits >= entry->capacity) {
        entry->capacity *= 2;
        entry->hits = (KmerHit*)realloc(entry->hits, entry->capacity * sizeof(KmerHit));
    }

    entry->hits[entry->num_hits].gene_id = gene_id;
    entry->hits[entry->num_hits].position = position;
    entry->num_hits++;
}

// Lookup k-mer in index
KmerEntry* kmer_lookup(KmerIndex* index, uint64_t kmer) {
    uint32_t hash = kmer % index->table_size;
    KmerEntry* entry = index->table[hash];

    while (entry && entry->kmer != kmer) {
        entry = entry->next;
    }

    return entry;
}

// Add gene to index
int index_add_gene(KmerIndex* index, const char* name, const char* sequence) {
    if (index->num_genes >= index->genes_capacity) {
        index->genes_capacity *= 2;
        index->genes = (Gene*)realloc(index->genes, index->genes_capacity * sizeof(Gene));
        if (!index->genes) return -1;
    }

    uint32_t gene_id = index->num_genes;
    Gene* gene = &index->genes[gene_id];

    strncpy(gene->name, name, MAX_GENE_NAME - 1);
    gene->name[MAX_GENE_NAME - 1] = '\0';

    gene->length = strlen(sequence);
    gene->sequence = (char*)malloc(gene->length + 1);
    if (!gene->sequence) return -1;
    strcpy(gene->sequence, sequence);

    // Add all k-mers from this gene
    for (uint32_t i = 0; i <= gene->length - KMER_SIZE; i++) {
        if (kmer_is_valid(&sequence[i])) {
            uint64_t kmer = kmer_encode(&sequence[i]);
            if (kmer != UINT64_MAX) {
                kmer_add_to_index(index, kmer, gene_id, i);
            }
        }
    }

    index->num_genes++;
    return gene_id;
}

// Parse FASTA and build index
int index_build_from_fasta(KmerIndex* index, const char* fasta_data, size_t fasta_size) {
    char gene_name[MAX_GENE_NAME] = {0};
    char* sequence = (char*)malloc(MAX_SEQUENCE_LENGTH);
    if (!sequence) return -1;

    uint32_t seq_pos = 0;
    int in_sequence = 0;
    int genes_added = 0;

    for (size_t i = 0; i < fasta_size; i++) {
        char c = fasta_data[i];

        if (c == '>') {
            // Save previous gene if exists
            if (in_sequence && seq_pos > 0) {
                sequence[seq_pos] = '\0';
                if (index_add_gene(index, gene_name, sequence) >= 0) {
                    genes_added++;
                }
                seq_pos = 0;
            }

            // Parse new gene name
            in_sequence = 1;
            size_t name_pos = 0;
            i++; // Skip '>'

            while (i < fasta_size && fasta_data[i] != '\n' && fasta_data[i] != '\r') {
                if (name_pos < MAX_GENE_NAME - 1) {
                    gene_name[name_pos++] = fasta_data[i];
                }
                i++;
            }
            gene_name[name_pos] = '\0';

        } else if (in_sequence && !isspace(c)) {
            if (seq_pos < MAX_SEQUENCE_LENGTH - 1) {
                sequence[seq_pos++] = toupper(c);
            }
        }
    }

    // Add last gene
    if (in_sequence && seq_pos > 0) {
        sequence[seq_pos] = '\0';
        if (index_add_gene(index, gene_name, sequence) >= 0) {
            genes_added++;
        }
    }

    free(sequence);
    return genes_added;
}

// Align a single read using winner-takes-all strategy
ReadAlignment* align_read(KmerIndex* index, const char* read_name, const char* sequence, uint32_t seq_len) {
    if (seq_len < KMER_SIZE) return NULL;

    ReadAlignment* result = (ReadAlignment*)calloc(1, sizeof(ReadAlignment));
    result->read_name = strdup(read_name);

    // Score array for all genes
    uint32_t* scores = (uint32_t*)calloc(index->num_genes, sizeof(uint32_t));
    uint32_t* coverage_bitmap = (uint32_t*)calloc(index->num_genes * ((MAX_SEQUENCE_LENGTH / 32) + 1), sizeof(uint32_t));

    uint32_t total_kmers = 0;

    // Extract k-mers from read and find matches
    for (uint32_t i = 0; i <= seq_len - KMER_SIZE; i++) {
        if (kmer_is_valid(&sequence[i])) {
            uint64_t kmer = kmer_encode(&sequence[i]);
            if (kmer == UINT64_MAX) continue;

            total_kmers++;

            KmerEntry* entry = kmer_lookup(index, kmer);
            if (entry) {
                // Add score for each gene hit by this k-mer
                for (uint32_t j = 0; j < entry->num_hits; j++) {
                    uint32_t gene_id = entry->hits[j].gene_id;
                    uint32_t pos = entry->hits[j].position;

                    scores[gene_id]++;

                    // Mark position as covered (for coverage calculation)
                    uint32_t bit_idx = gene_id * ((MAX_SEQUENCE_LENGTH / 32) + 1) + (pos / 32);
                    coverage_bitmap[bit_idx] |= (1U << (pos % 32));
                }
            }
        }
    }

    result->num_kmers_in_read = total_kmers;

    // Winner-takes-all: find gene with highest score
    uint32_t best_gene = 0;
    uint32_t best_score = 0;

    for (uint32_t i = 0; i < index->num_genes; i++) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            best_gene = i;
        }
    }

    // Calculate coverage and identity for best hit
    if (best_score > 0) {
        result->best_hit.gene_id = best_gene;
        result->best_hit.score = best_score;

        // Calculate coverage (fraction of gene with at least one k-mer hit)
        uint32_t gene_len = index->genes[best_gene].length;
        uint32_t covered_positions = 0;

        uint32_t base_idx = best_gene * ((MAX_SEQUENCE_LENGTH / 32) + 1);
        for (uint32_t pos = 0; pos < gene_len; pos++) {
            uint32_t bit_idx = base_idx + (pos / 32);
            if (coverage_bitmap[bit_idx] & (1U << (pos % 32))) {
                covered_positions++;
            }
        }

        result->best_hit.coverage = (float)covered_positions / gene_len;

        // Estimate identity (k-mer matches / possible k-mers)
        uint32_t max_possible_kmers = (seq_len >= gene_len) ? gene_len - KMER_SIZE + 1 : seq_len - KMER_SIZE + 1;
        result->best_hit.identity = (float)best_score / max_possible_kmers;
        if (result->best_hit.identity > 1.0f) result->best_hit.identity = 1.0f;

    } else {
        result->best_hit.gene_id = UINT32_MAX; // No hit
        result->best_hit.score = 0;
        result->best_hit.coverage = 0.0f;
        result->best_hit.identity = 0.0f;
    }

    free(scores);
    free(coverage_bitmap);

    return result;
}

// Free alignment result
void alignment_destroy(ReadAlignment* aln) {
    if (!aln) return;
    if (aln->read_name) free(aln->read_name);
    free(aln);
}

// Print alignment result
void print_alignment(ReadAlignment* aln, const KmerIndex* index) {
    if (!aln) return;

    printf("Read: %s\n", aln->read_name);

    if (aln->best_hit.gene_id == UINT32_MAX) {
        printf("  No hit found\n");
    } else {
        const Gene* gene = &index->genes[aln->best_hit.gene_id];
        printf("  Best hit: %s\n", gene->name);
        printf("  Score: %u k-mer matches\n", aln->best_hit.score);
        printf("  Coverage: %.2f%%\n", aln->best_hit.coverage * 100);
        printf("  Identity: %.2f%%\n", aln->best_hit.identity * 100);
    }
}

// Parse FASTQ and align all reads
int align_fastq(KmerIndex* index, const char* fastq_data, size_t fastq_size,
                ReadAlignment*** results, uint32_t* num_results) {

    // Count reads first
    uint32_t read_count = 0;
    for (size_t i = 0; i < fastq_size; i++) {
        if (fastq_data[i] == '@' && (i == 0 || fastq_data[i-1] == '\n')) {
            read_count++;
        }
    }

    if (read_count == 0) return 0;

    *results = (ReadAlignment**)malloc(read_count * sizeof(ReadAlignment*));
    if (!*results) return -1;

    *num_results = 0;

    char read_name[MAX_GENE_NAME];
    char* sequence = (char*)malloc(MAX_SEQUENCE_LENGTH);
    if (!sequence) {
        free(*results);
        return -1;
    }

    size_t i = 0;
    while (i < fastq_size) {
        // Parse read name
        if (fastq_data[i] != '@') {
            // Skip to next @ line
            while (i < fastq_size && fastq_data[i] != '\n') i++;
            i++;
            continue;
        }

        i++; // Skip '@'
        size_t name_pos = 0;
        while (i < fastq_size && fastq_data[i] != '\n' && fastq_data[i] != ' ' && fastq_data[i] != '\t') {
            if (name_pos < MAX_GENE_NAME - 1) {
                read_name[name_pos++] = fastq_data[i];
            }
            i++;
        }
        read_name[name_pos] = '\0';

        // Skip to end of line
        while (i < fastq_size && fastq_data[i] != '\n') i++;
        i++; // Skip newline

        // Parse sequence
        size_t seq_pos = 0;
        while (i < fastq_size && fastq_data[i] != '\n' && fastq_data[i] != '+') {
            if (!isspace(fastq_data[i]) && seq_pos < MAX_SEQUENCE_LENGTH - 1) {
                sequence[seq_pos++] = toupper(fastq_data[i]);
            }
            i++;
        }
        sequence[seq_pos] = '\0';

        // Skip quality scores (+ line and quality line)
        while (i < fastq_size && fastq_data[i] != '\n') i++; // Skip to end of + line
        i++; // Skip newline
        while (i < fastq_size && fastq_data[i] != '\n') i++; // Skip quality line
        i++; // Skip newline

        // Align this read
        if (seq_pos >= KMER_SIZE) {
            ReadAlignment* aln = align_read(index, read_name, sequence, seq_pos);
            if (aln) {
                (*results)[*num_results] = aln;
                (*num_results)++;
            }
        }
    }

    free(sequence);
    return *num_results;
}
