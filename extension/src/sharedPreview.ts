import * as path from 'path';

// Native module interface
interface SharedPreviewNative {
    open(name: string): boolean;
    close(): void;
    isOpen(): boolean;
    getHeader(): {
        magic: number;
        version: number;
        operatorCount: number;
        frameNumber: number;
        timestampUs: number;
    } | null;
    getSlot(index: number): {
        operatorId: string;
        sourceLine: number;
        frameNumber: number;
        width: number;
        height: number;
        kind: number;
        ready: boolean;
        pixels?: Buffer;
        value?: number;
        values?: number[];
    } | null;
    getSlotPixels(index: number): Buffer | null;
    THUMB_WIDTH: number;
    THUMB_HEIGHT: number;
    THUMB_CHANNELS: number;
    THUMB_SIZE: number;
    MAX_OPERATORS: number;
}

let native: SharedPreviewNative | null = null;
let isInitialized = false;

export const PREVIEW_KIND = {
    TEXTURE: 0,
    VALUE: 1,
    VALUE_ARRAY: 2,
    GEOMETRY: 3
} as const;

export interface PreviewHeader {
    magic: number;
    version: number;
    operatorCount: number;
    frameNumber: number;
    timestampUs: number;
}

export interface PreviewSlot {
    operatorId: string;
    sourceLine: number;
    frameNumber: number;
    width: number;
    height: number;
    kind: number;
    ready: boolean;
    pixels?: Buffer;
    value?: number;
    values?: number[];
}

export function initSharedPreview(extensionPath: string): boolean {
    if (isInitialized) {
        return native !== null;
    }

    isInitialized = true;

    try {
        // Try to load the native module from the extension's native/build directory
        const nativePath = path.join(extensionPath, 'native', 'build', 'Release', 'shared_preview.node');
        native = require(nativePath);
        console.log('[SharedPreview] Native module loaded successfully');
        return true;
    } catch (e) {
        console.warn('[SharedPreview] Failed to load native module:', e);
        console.warn('[SharedPreview] Shared memory previews will not be available');
        native = null;
        return false;
    }
}

export function openSharedMemory(name: string = 'vivid_preview'): boolean {
    if (!native) {
        return false;
    }

    try {
        const result = native.open(name);
        if (result) {
            console.log(`[SharedPreview] Opened shared memory: ${name}`);
        }
        return result;
    } catch (e) {
        console.error('[SharedPreview] Error opening shared memory:', e);
        return false;
    }
}

export function closeSharedMemory(): void {
    if (native) {
        native.close();
    }
}

export function isSharedMemoryOpen(): boolean {
    return native?.isOpen() ?? false;
}

export function getHeader(): PreviewHeader | null {
    return native?.getHeader() ?? null;
}

export function getSlot(index: number): PreviewSlot | null {
    return native?.getSlot(index) ?? null;
}

export function getSlotPixels(index: number): Buffer | null {
    return native?.getSlotPixels(index) ?? null;
}

export function getConstants() {
    if (!native) {
        return {
            THUMB_WIDTH: 128,
            THUMB_HEIGHT: 128,
            THUMB_CHANNELS: 3,
            THUMB_SIZE: 128 * 128 * 3,
            MAX_OPERATORS: 64
        };
    }
    return {
        THUMB_WIDTH: native.THUMB_WIDTH,
        THUMB_HEIGHT: native.THUMB_HEIGHT,
        THUMB_CHANNELS: native.THUMB_CHANNELS,
        THUMB_SIZE: native.THUMB_SIZE,
        MAX_OPERATORS: native.MAX_OPERATORS
    };
}

// Convert RGB pixel buffer to base64 data URL (for display in webviews)
// Uses BMP format which is universally supported by browsers
export function pixelsToDataUrl(pixels: Buffer, width: number = 128, height: number = 128): string {
    // BMP file format (Windows Bitmap)
    // BMP stores pixels bottom-to-top and in BGR order

    const rowSize = Math.ceil((width * 3) / 4) * 4;  // Rows must be 4-byte aligned
    const pixelDataSize = rowSize * height;
    const fileSize = 54 + pixelDataSize;  // 14 (file header) + 40 (info header) + pixel data

    const bmp = Buffer.alloc(fileSize);
    let offset = 0;

    // BMP File Header (14 bytes)
    bmp.write('BM', offset); offset += 2;                    // Signature
    bmp.writeUInt32LE(fileSize, offset); offset += 4;        // File size
    bmp.writeUInt32LE(0, offset); offset += 4;               // Reserved
    bmp.writeUInt32LE(54, offset); offset += 4;              // Pixel data offset

    // DIB Header (BITMAPINFOHEADER - 40 bytes)
    bmp.writeUInt32LE(40, offset); offset += 4;              // Header size
    bmp.writeInt32LE(width, offset); offset += 4;            // Width
    bmp.writeInt32LE(height, offset); offset += 4;           // Height (positive = bottom-up)
    bmp.writeUInt16LE(1, offset); offset += 2;               // Color planes
    bmp.writeUInt16LE(24, offset); offset += 2;              // Bits per pixel (24 = RGB)
    bmp.writeUInt32LE(0, offset); offset += 4;               // Compression (0 = none)
    bmp.writeUInt32LE(pixelDataSize, offset); offset += 4;   // Image size
    bmp.writeInt32LE(2835, offset); offset += 4;             // X pixels per meter
    bmp.writeInt32LE(2835, offset); offset += 4;             // Y pixels per meter
    bmp.writeUInt32LE(0, offset); offset += 4;               // Colors in color table
    bmp.writeUInt32LE(0, offset); offset += 4;               // Important colors

    // Pixel data (bottom-to-top, BGR order)
    for (let y = height - 1; y >= 0; y--) {
        for (let x = 0; x < width; x++) {
            const srcIdx = (y * width + x) * 3;
            const r = pixels[srcIdx] || 0;
            const g = pixels[srcIdx + 1] || 0;
            const b = pixels[srcIdx + 2] || 0;

            // BMP uses BGR order
            bmp[offset++] = b;
            bmp[offset++] = g;
            bmp[offset++] = r;
        }
        // Pad row to 4-byte boundary
        const padding = rowSize - (width * 3);
        for (let p = 0; p < padding; p++) {
            bmp[offset++] = 0;
        }
    }

    return `data:image/bmp;base64,${bmp.toString('base64')}`;
}

// Alias for compatibility
export function pixelsToPngDataUrl(pixels: Buffer, width: number = 128, height: number = 128): string {
    return pixelsToDataUrl(pixels, width, height);
}
