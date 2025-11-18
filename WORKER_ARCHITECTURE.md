# Swiftomics Worker Architecture

## Overview

Swiftomics uses a generalized Web Worker architecture to run bioinformatics tools in the browser without blocking the UI. The worker system is designed to be extensible, allowing new tools to be added easily.

## Architecture

### Core Components

1. **biotools-worker.js** - General-purpose Web Worker that can run any configured bioinformatics tool
2. **TOOL_CONFIGS** - Configuration object that defines how each tool behaves
3. **index.html** - Main UI that communicates with the worker

## Key Features

### 1. Automatic MIME Type Detection

The worker now automatically detects the correct MIME type for output files based on their extensions:

- `.fastq`, `.fq` → `text/plain`
- `.fastq.gz`, `.fq.gz` → `application/gzip`
- `.fasta`, `.fa` → `text/plain`
- `.html` → `text/html`
- `.json` → `application/json`
- `.bam` → `application/octet-stream`

This fixes the bug where non-gzipped FASTQ files were incorrectly labeled as `application/gzip`.

### 2. Tool Configuration System

Each tool is configured with:
- Script path (e.g., `fastp.js`)
- WebAssembly path (e.g., `fastp.wasm`)
- Data file path (if needed)
- Output patterns with argument flags and MIME types

Example configuration:

```javascript
'fastp': {
    scriptPath: 'fastp-0.20.1/fastp.js',
    wasmPath: 'fastp-0.20.1/fastp.wasm',
    dataPath: 'fastp-0.20.1/fastp.data',
    outputPatterns: [
        { argFlag: '-o', type: 'output', mimeType: 'auto' },
        { argFlag: '-h', type: 'report', mimeType: 'text/html' },
        { argFlag: '-j', type: 'report', mimeType: 'application/json' }
    ]
}
```

### 3. UI Structure

The results and terminal output are organized hierarchically:

```
File Row
└── Results Row (visible when results OR logs exist)
    ├── Results Section (collapsible)
    │   └── List of output files
    └── Terminal Output Section (collapsible)
        └── Console logs from the tool
```

This ensures that:
- Terminal output is always associated with its file
- Terminal output appears even if there are no results (e.g., if the tool errors out)
- Both sections are collapsible for a clean UI

## Adding a New Tool

To add a new bioinformatics tool:

1. **Compile the tool to WebAssembly** using Emscripten
2. **Add configuration** to `TOOL_CONFIGS` in `biotools-worker.js`:

```javascript
'your-tool': {
    scriptPath: 'your-tool/your-tool.js',
    wasmPath: 'your-tool/your-tool.wasm',
    dataPath: 'your-tool/your-tool.data',  // optional
    outputPatterns: [
        { argFlag: '--output', type: 'output', mimeType: 'auto' },
        // Add more output patterns as needed
    ]
}
```

3. **Add UI element** in `index.html`:

```html
<div class="tool-card" data-tool="your-tool" draggable="true">
    <span style="font-size:1.5rem;margin-right:0.5rem">🧬</span>
    <div style="flex:1">
        <div style="font-weight:600;font-size:0.875rem">Your Tool</div>
        <div style="font-size:0.7rem;color:var(--text-light)">Description</div>
    </div>
</div>
```

4. **Create tool execution function** (following the `runToolOnFile` pattern)

## Fixed Issues

### Issue 1: Missing Filtered FASTQ for Non-Gzipped Files

**Problem**: When running fastp on `.fastq` files (not `.fastq.gz`), the filtered output file was not appearing in results, only the HTML and JSON reports.

**Root Cause**:
- The worker hardcoded output MIME type as `application/gzip` (line 80 in old `fastp-worker.js`)
- Non-gzipped FASTQ files couldn't be properly handled

**Solution**:
- Implemented automatic MIME type detection based on file extension
- Set `mimeType: 'auto'` in configuration, which triggers detection via `getMimeTypeFromFilename()`

### Issue 2: Terminal Output Position

**Problem**: Request to keep terminal output inside results section.

**Status**: Already correctly implemented (lines 910-960 in `index.html`). Terminal output is inside the `results-row` which only renders when `fileObj.results.length > 0 || fileObj.logs.length > 0`.

### Issue 3: Tool-Specific Worker

**Problem**: The old `fastp-worker.js` was tightly coupled to fastp, making it difficult to add new tools.

**Solution**:
- Created `biotools-worker.js` - a general-purpose worker
- Implemented `TOOL_CONFIGS` system for declarative tool configuration
- Tools are now initialized with `{ type: 'init', data: { toolName: 'fastp' } }`

### Issue 4: Large File Memory Issues ("Invalid array length")

**Problem**: When reading large output files (e.g., 90MB FASTQ files), the worker would fail with "Invalid array length" error during file transfer.

**Root Cause**:
- Converting large Uint8Array to regular arrays using `Array.from(fileData)` caused memory allocation failures
- Inefficient data transfer between worker and main thread created unnecessary copies of large files

**Solution**:
- Keep data as `Uint8Array` instead of converting to regular arrays
- Use **Transferable Objects** for zero-copy transfer of ArrayBuffers between worker and main thread
- Modified worker code (line 149 in `biotools-worker.js`) to create `Uint8Array` copy instead of array
- Updated worker to transfer buffers efficiently using `postMessage(msg, transferables)` (line 172)
- Updated main thread to receive data directly without additional conversion (line 1413 in `index.html`)

## Testing

To test the fixes:

1. **Test with `.fastq` files**:
   - Upload a plain FASTQ file (not gzipped)
   - Drag fastp tool onto it
   - Verify all 3 outputs appear: filtered FASTQ, HTML report, JSON report
   - Check MIME types in browser console

2. **Test with `.fastq.gz` files**:
   - Upload a gzipped FASTQ file
   - Drag fastp tool onto it
   - Verify all 3 outputs appear with correct MIME types

3. **Test terminal output**:
   - Run fastp on any file
   - Verify terminal output appears in collapsible section
   - Verify it's inside the results row
   - Test with files that error out (terminal should still appear)

4. **Test with large files**:
   - Upload a large FASTQ file (50MB+)
   - Drag fastp tool onto it
   - Verify all outputs appear without "Invalid array length" error
   - Check browser console for any memory errors
   - Verify filtered FASTQ file can be downloaded successfully

## Migration Notes

If you have existing code using the old `fastp-worker.js`:

1. Replace `fastp-worker.js` with `biotools-worker.js`
2. Update initialization:
   ```javascript
   // Old
   worker.postMessage({ type: 'init' });

   // New
   worker.postMessage({
       type: 'init',
       data: { toolName: 'fastp' }
   });
   ```
3. Update run command:
   ```javascript
   // Old
   worker.postMessage({
       type: 'run',
       data: { inputFileName, inputFileData, args }
   });

   // New
   worker.postMessage({
       type: 'run',
       data: { toolName: 'fastp', inputFileName, inputFileData, args }
   });
   ```

## Performance Considerations

- Worker runs in separate thread (no UI blocking)
- WebAssembly provides near-native performance
- Files are processed entirely in browser memory
- No server communication required
- **Transferable Objects** used for zero-copy transfer of large files between worker and main thread
- Efficient memory management supports processing large files (100MB+) without issues

## Security

- All processing happens client-side
- User data never leaves the browser
- WebAssembly sandboxing provides isolation
- No external dependencies at runtime
