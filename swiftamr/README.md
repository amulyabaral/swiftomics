# SwiftAMR - Fast K-mer Based AMR Gene Detection

SwiftAMR is a lightweight, browser-based tool for detecting antimicrobial resistance (AMR) genes in metagenomic FASTQ files using k-mer alignment with a winner-takes-all strategy.

## Features

- **Fast k-mer indexing**: 16-mer based approach optimized for AMR gene databases
- **Winner-takes-all strategy**: Each read is assigned to the gene with the highest k-mer score
- **Browser-based**: Runs entirely in the browser using WebAssembly - no server required
- **Memory efficient**: Handles large FASTQ files and AMR databases efficiently
- **Redundant database friendly**: Unlike Minimap2, SwiftAMR is specifically designed for redundant AMR databases

## Algorithm

### Phase 1: Index Building (One-time per database)
1. Parse AMR gene database (FASTA format)
2. Extract all 16-mers from each gene
3. Build hash table: k-mer → list of (gene_id, position)

### Phase 2: Read Alignment (Runtime)
For each FASTQ read:
1. Extract all valid 16-mers from the read
2. Query index to find matching genes
3. Score each gene by number of k-mer matches
4. Apply **winner-takes-all**: select gene with highest score
5. Calculate coverage (fraction of gene covered by k-mers)
6. Estimate identity (k-mer matches / possible k-mers)

## Usage in Swiftomics

### 1. Load AMR Database
- Click on the SwiftAMR tool card in the left panel
- Upload an AMR gene database in FASTA format (e.g., CARD, MEGARes, ResFinder)
- Database will be loaded and shown in the status indicator
- Click "Save" to enable the tool

### 2. Run Alignment
- Drag the SwiftAMR tool onto a FASTQ file
- The tool will:
  - Build k-mer index from the database
  - Align all reads in the FASTQ file
  - Generate TSV output with results

### 3. View Results
Results are in TSV format with columns:
- `read_name`: FASTQ read identifier
- `gene`: Best matching AMR gene (or "No_hit" if no match)
- `score`: Number of k-mer matches
- `coverage`: Fraction of gene covered by k-mers (0.0-1.0)
- `identity`: Estimated sequence identity (0.0-1.0)

## Test Files

The repository includes test files:
- `test_amr_db.fasta`: Small AMR database with 6 genes (mecA, blaTEM-1, vanA, aadA, ermB, qnrA)
- `test_amr_reads.fastq`: Test reads containing fragments from the AMR genes

## Building from Source

### Prerequisites
- Emscripten SDK (for WebAssembly compilation)
- GCC (for native compilation)

### Compile to WebAssembly
```bash
cd swiftamr
source ../emsdk/emsdk_env.sh
make wasm
```

This generates:
- `swiftamr.js`: JavaScript glue code
- `swiftamr.wasm`: WebAssembly binary

### Compile Native Binary (for testing)
```bash
make native
./swiftamr test_amr_db.fasta test_amr_reads.fastq
```

## Architecture

### Core Components

1. **swiftamr.h**: Header file with data structures and function declarations
2. **swiftamr.c**: Core k-mer indexing and alignment algorithms
3. **main.c**: WASM-exported functions and native test harness
4. **Makefile**: Build system for both native and WASM targets

### Data Structures

```c
// K-mer index entry
typedef struct KmerEntry {
    uint64_t kmer;           // Encoded k-mer (2 bits per nucleotide)
    KmerHit* hits;           // Array of gene hits
    uint32_t num_hits;       // Number of hits
    struct KmerEntry* next;  // Collision chaining
} KmerEntry;

// K-mer index
typedef struct {
    KmerEntry** table;       // Hash table
    Gene* genes;             // Array of genes
    uint32_t num_genes;      // Gene count
} KmerIndex;

// Alignment result
typedef struct {
    uint32_t gene_id;        // Best matching gene
    uint32_t score;          // K-mer matches
    float coverage;          // Gene coverage
    float identity;          // Estimated identity
} AlignmentResult;
```

### Key Parameters

- **K-mer size**: 16 nucleotides (configurable via `KMER_SIZE`)
- **Hash table size**: 16M entries (2^24)
- **Max gene name**: 256 characters
- **Max sequence length**: 100 MB

## Performance Considerations

- **Index building**: O(N × L) where N = number of genes, L = average gene length
- **Read alignment**: O(R × M) where R = number of reads, M = average read length
- **Memory usage**: ~few hundred MB for typical AMR databases
- **WASM overhead**: ~50-80% of native C performance

## Advantages vs. Minimap2

1. **Redundant databases**: SwiftAMR handles overlapping/similar genes better
2. **Winner-takes-all**: Clear assignment of each read to single gene
3. **Lightweight**: No need for complex alignment algorithms
4. **Browser-native**: No server infrastructure required

## Limitations

- Fixed k-mer size (16-mer)
- No gapped alignment
- Sensitive to sequencing errors (though k-mer redundancy helps)
- Memory constraints in browser (typically 2-4 GB limit)

## Future Enhancements

- [ ] Variable k-mer sizes
- [ ] Spaced seeds for better sensitivity
- [ ] Multi-threading support
- [ ] Gzip FASTQ support
- [ ] Paired-end read support
- [ ] Quality score filtering
- [ ] JSON output format
- [ ] Visualization of coverage plots

## License

This tool is part of the Swiftomics project.

## Citation

If you use SwiftAMR in your research, please cite:
- Swiftomics: Browser-based bioinformatics tools using WebAssembly
