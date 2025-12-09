// General Web Worker for running bioinformatics tools without blocking the UI
// This worker runs in a separate thread and communicates via message passing

let toolModule = null;
let isInitialized = false;
let currentTool = null;

// Tool configurations - defines output patterns and behavior for each tool
const TOOL_CONFIGS = {
    'fastp': {
        scriptPath: 'fastp-0.20.1/fastp.js',
        wasmPath: 'fastp-0.20.1/fastp.wasm',
        dataPath: 'fastp-0.20.1/fastp.data',
        outputPatterns: [
            { argFlag: '-o', type: 'output', mimeType: 'auto' },  // R1 output (auto-detect from extension)
            { argFlag: '-O', type: 'output', mimeType: 'auto' },  // R2 output for paired-end
            { argFlag: '-h', type: 'report', mimeType: 'text/html' },
            { argFlag: '-j', type: 'report', mimeType: 'application/json' }
        ]
    },
    'bowtie2': {
        scriptPath: 'bowtie2-align-s-2.4.2/bowtie2-align-s.js',
        wasmPath: 'bowtie2-align-s-2.4.2/bowtie2-align-s.wasm',
        dataPath: 'bowtie2-align-s-2.4.2/bowtie2-align-s.data',
        indexFiles: [
            'megares_files/megares.1.bt2',
            'megares_files/megares.2.bt2',
            'megares_files/megares.3.bt2',
            'megares_files/megares.4.bt2',
            'megares_files/megares.rev.1.bt2',
            'megares_files/megares.rev.2.bt2'
        ],
        outputPatterns: [
            { argFlag: '-S', type: 'output', mimeType: 'text/plain' }  // SAM output
        ]
    },
    'swiftamr': {
        scriptPath: 'swiftamr/swiftamr.js',
        wasmPath: 'swiftamr/swiftamr.wasm',
        requiresDatabase: true,  // Needs AMR gene database
        isCustom: true,  // Custom integration (not standard CLI tool)
        outputPatterns: [
            { type: 'output', mimeType: 'text/plain', extension: '.tsv' }  // TSV results
        ]
    }
    // Future tools can be added here:
    // 'bwa': { scriptPath: 'bwa/bwa.js', ... },
    // 'samtools': { scriptPath: 'samtools/samtools.js', ... }
};

// Helper function to determine MIME type from filename
function getMimeTypeFromFilename(filename) {
    const ext = filename.toLowerCase();

    if (ext.endsWith('.gz')) {
        return 'application/gzip';
    } else if (ext.endsWith('.fastq') || ext.endsWith('.fq')) {
        return 'text/plain';
    } else if (ext.endsWith('.fasta') || ext.endsWith('.fa')) {
        return 'text/plain';
    } else if (ext.endsWith('.html')) {
        return 'text/html';
    } else if (ext.endsWith('.json')) {
        return 'application/json';
    } else if (ext.endsWith('.bam')) {
        return 'application/octet-stream';
    } else if (ext.endsWith('.sam')) {
        return 'text/plain';
    }

    return 'application/octet-stream';
}

// Listen for messages from the main thread
self.onmessage = async function(e) {
    const { type, data } = e.data;

    if (type === 'init') {
        // Initialize tool WebAssembly module
        try {
            const { toolName } = data;

            if (!TOOL_CONFIGS[toolName]) {
                throw new Error(`Unknown tool: ${toolName}`);
            }

            currentTool = toolName;
            const config = TOOL_CONFIGS[toolName];

            // Import the tool module script
            importScripts(config.scriptPath);

            // Initialize the module - different for custom tools
            if (config.isCustom && toolName === 'swiftamr') {
                toolModule = await createSwiftAMRModule({
                    locateFile: (path) => {
                        if (path.endsWith('.wasm')) {
                            return config.wasmPath;
                        }
                        return path;
                    },
                    print: (text) => {
                        self.postMessage({ type: 'stdout', text });
                    },
                    printErr: (text) => {
                        self.postMessage({ type: 'stderr', text });
                    }
                });

                // Debug: check what's available
                self.postMessage({ type: 'log', text: `Module properties: ${Object.keys(toolModule).filter(k => k.includes('HEA') || k.includes('memory') || k.includes('malloc')).join(', ')}` });
            } else {
                toolModule = await Module({
                locateFile: (path) => {
                    if (path.endsWith('.wasm')) {
                        return config.wasmPath;
                    }
                    if (path.endsWith('.data')) {
                        return config.dataPath;
                    }
                    return path;
                },
                print: (text) => {
                    // Send stdout back to main thread
                    self.postMessage({ type: 'stdout', text });
                },
                printErr: (text) => {
                    // Send stderr back to main thread
                    self.postMessage({ type: 'stderr', text });
                }
            });
            }

            // Preload index files if specified (e.g., for Bowtie2)
            if (config.indexFiles && config.indexFiles.length > 0) {
                self.postMessage({ type: 'log', text: `Loading ${config.indexFiles.length} index files...` });

                for (const indexPath of config.indexFiles) {
                    try {
                        const response = await fetch(indexPath);
                        if (!response.ok) {
                            throw new Error(`Failed to fetch ${indexPath}: ${response.statusText}`);
                        }
                        const arrayBuffer = await response.arrayBuffer();
                        const uint8Array = new Uint8Array(arrayBuffer);

                        // Extract just the filename from the path
                        const fileName = indexPath.split('/').pop();

                        // Write to virtual filesystem
                        toolModule.FS.writeFile(fileName, uint8Array);
                        self.postMessage({ type: 'log', text: `Loaded index file: ${fileName}` });
                    } catch (error) {
                        self.postMessage({
                            type: 'log',
                            text: `Failed to load index file ${indexPath}: ${error.message}`,
                            isError: true
                        });
                        throw error;
                    }
                }

                self.postMessage({ type: 'log', text: 'All index files loaded successfully' });
            }

            isInitialized = true;
            self.postMessage({
                type: 'init-complete',
                success: true,
                toolName: toolName
            });
        } catch (error) {
            self.postMessage({
                type: 'init-complete',
                success: false,
                error: error.message
            });
        }
    } else if (type === 'run') {
        // Run tool with the provided parameters
        try {
            if (!isInitialized || !toolModule) {
                throw new Error(`${currentTool} module not initialized`);
            }

            const { inputFileName, inputFileData, inputFileName2, inputFileData2, args, toolName } = data;

            if (toolName !== currentTool) {
                throw new Error(`Worker initialized for ${currentTool}, but asked to run ${toolName}`);
            }

            const config = TOOL_CONFIGS[toolName];

            // Write input file(s) to virtual filesystem (skip for custom tools like SwiftAMR)
            if (!config.isCustom) {
                const uint8Array = new Uint8Array(inputFileData);
                toolModule.FS.writeFile(inputFileName, uint8Array);
                self.postMessage({ type: 'log', text: `Mounted: ${inputFileName}` });

                // If paired-end, write second file
                if (inputFileName2 && inputFileData2) {
                    const uint8Array2 = new Uint8Array(inputFileData2);
                    toolModule.FS.writeFile(inputFileName2, uint8Array2);
                    self.postMessage({ type: 'log', text: `Mounted: ${inputFileName2}` });
                }
            }

            self.postMessage({ type: 'log', text: `Running: ${toolName} ${args.join(' ')}` });
            self.postMessage({ type: 'log', text: `--- ${toolName} output ---` });

            let returnCode = 0;
            const outputFiles = [];

            // Run tool - custom handling for SwiftAMR
            if (config.isCustom && toolName === 'swiftamr') {
                // SwiftAMR works directly with memory buffers (no virtual filesystem)
                const databaseData = data.databaseData;

                if (!databaseData) {
                    throw new Error('SwiftAMR requires AMR database FASTA file');
                }

                self.postMessage({ type: 'log', text: `Loading AMR database (${(databaseData.byteLength / 1024 / 1024).toFixed(2)} MB)...` });

                // Build k-mer index from FASTA database
                const dbUint8Array = new Uint8Array(databaseData);

                // Wrap the C functions
                const malloc = toolModule._malloc;
                const free = toolModule._free;
                const buildIndex = toolModule.cwrap('swiftamr_build_index', 'number', ['number', 'number']);

                // Allocate memory and copy data
                const dbPtr = malloc(dbUint8Array.length);

                // Write to WASM memory - try multiple methods
                if (toolModule.writeArrayToMemory) {
                    toolModule.writeArrayToMemory(dbUint8Array, dbPtr);
                } else if (toolModule.HEAPU8) {
                    toolModule.HEAPU8.set(dbUint8Array, dbPtr);
                } else {
                    // Access the raw memory buffer
                    const memoryBuffer = toolModule.wasmMemory ? toolModule.wasmMemory.buffer : toolModule.memory.buffer;
                    const heap = new Uint8Array(memoryBuffer);
                    heap.set(dbUint8Array, dbPtr);
                }

                const genesAdded = buildIndex(dbPtr, dbUint8Array.length);
                free(dbPtr);

                if (genesAdded < 0) {
                    throw new Error('Failed to build k-mer index from database');
                }

                self.postMessage({ type: 'log', text: `Built k-mer index: ${genesAdded} genes` });

                // Align FASTQ reads
                const fastqUint8Array = new Uint8Array(inputFileData);
                self.postMessage({ type: 'log', text: `Aligning ${inputFileName} (${(fastqUint8Array.length / 1024).toFixed(2)} KB)...` });

                const alignFastq = toolModule.cwrap('swiftamr_align_fastq', 'number', ['number', 'number']);
                const fastqPtr = malloc(fastqUint8Array.length);

                // Write FASTQ data to memory
                if (toolModule.writeArrayToMemory) {
                    toolModule.writeArrayToMemory(fastqUint8Array, fastqPtr);
                } else if (toolModule.HEAPU8) {
                    toolModule.HEAPU8.set(fastqUint8Array, fastqPtr);
                } else {
                    const memoryBuffer = toolModule.wasmMemory ? toolModule.wasmMemory.buffer : toolModule.memory.buffer;
                    const heap = new Uint8Array(memoryBuffer);
                    heap.set(fastqUint8Array, fastqPtr);
                }

                const resultsPtr = alignFastq(fastqPtr, fastqUint8Array.length);
                free(fastqPtr);

                // Read results (TSV string)
                const results = toolModule.UTF8ToString(resultsPtr);
                free(resultsPtr);

                self.postMessage({ type: 'log', text: `Alignment complete` });

                // Create output file
                const outputFileName = inputFileName.replace(/\.(fastq|fq)(\.gz)?$/i, '_amr_results.tsv');
                const encoder = new TextEncoder();
                const resultsData = encoder.encode(results);

                outputFiles.push({
                    name: outputFileName,
                    data: resultsData,
                    type: 'text/plain',
                    size: resultsData.byteLength,
                    category: 'output'
                });

                self.postMessage({ type: 'log', text: `Created: ${outputFileName} (${resultsData.byteLength} bytes)` });

                // Cleanup
                const cleanup = toolModule.cwrap('swiftamr_cleanup', null, []);
                cleanup();

            } else {
                // Standard tool execution (fastp, bowtie2, etc.)
                returnCode = toolModule.callMain(args);

                self.postMessage({ type: 'log', text: `--- ${toolName} completed with exit code: ${returnCode} ---` });

                // Debug: List all files in virtual filesystem
                try {
                    const files = toolModule.FS.readdir('/');
                    self.postMessage({ type: 'log', text: `Files in virtual FS: ${files.join(', ')}` });
                } catch (e) {
                    self.postMessage({ type: 'log', text: `Could not list FS files: ${e.message}` });
                }

                // Read output files from virtual filesystem based on tool configuration
                // Parse arguments to find output file paths
                self.postMessage({ type: 'log', text: `Checking for ${config.outputPatterns.length} output patterns...` });

                for (const pattern of config.outputPatterns) {
                    const argIndex = args.indexOf(pattern.argFlag);
                    self.postMessage({ type: 'log', text: `Looking for ${pattern.argFlag}: found at index ${argIndex}` });

                    if (argIndex !== -1 && argIndex + 1 < args.length) {
                        try {
                            const outputFileName = args[argIndex + 1];
                            self.postMessage({ type: 'log', text: `Attempting to read: ${outputFileName}` });

                            const fileData = toolModule.FS.readFile(outputFileName, { encoding: 'binary' });

                            // Determine MIME type
                            let mimeType = pattern.mimeType;
                            if (mimeType === 'auto') {
                                mimeType = getMimeTypeFromFilename(outputFileName);
                            }

                            // Create a copy of the file data to transfer
                            // Use Uint8Array instead of Array.from() to avoid memory issues with large files
                            const fileDataCopy = new Uint8Array(fileData);

                            outputFiles.push({
                                name: outputFileName,
                                data: fileDataCopy,
                                type: mimeType,
                                size: fileData.byteLength,
                                category: pattern.type
                            });

                            self.postMessage({ type: 'log', text: `Saved: ${outputFileName} (${mimeType})` });
                        } catch (e) {
                            self.postMessage({
                                type: 'log',
                                text: `Could not read ${pattern.argFlag} file: ${e.message}`,
                                isError: true
                            });
                        }
                    }
                }
            }

            // Send completion message with output files
            // Use transferable objects for efficient transfer of large files
            const transferables = outputFiles.map(f => f.data.buffer);
            self.postMessage({
                type: 'complete',
                success: true,
                outputFiles,
                returnCode
            }, transferables);

        } catch (error) {
            self.postMessage({
                type: 'complete',
                success: false,
                error: error.message
            });
        }
    }
};
