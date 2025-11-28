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
export function pixelsToDataUrl(pixels: Buffer, width: number = 128, height: number = 128): string {
    // Create a simple PPM image (easy to generate, browsers can display)
    // PPM format: P6\n<width> <height>\n255\n<rgb data>
    const header = `P6\n${width} ${height}\n255\n`;
    const headerBuffer = Buffer.from(header, 'ascii');
    const ppmBuffer = Buffer.concat([headerBuffer, pixels]);

    // Convert to base64
    const base64 = ppmBuffer.toString('base64');
    return `data:image/x-portable-pixmap;base64,${base64}`;
}

// Alternative: Convert to PNG using a simple encoder
// For VS Code webviews, we might need a more compatible format
export function pixelsToPngDataUrl(pixels: Buffer, width: number = 128, height: number = 128): string {
    // For now, just use PPM which most browsers support
    // Could add proper PNG encoding later if needed
    return pixelsToDataUrl(pixels, width, height);
}
