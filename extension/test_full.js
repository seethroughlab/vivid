// Full integration test - WebSocket + shared memory
const WebSocket = require('ws');
const sharedPreview = require('./native/build/Release/shared_preview.node');

const ws = new WebSocket('ws://localhost:9876');

ws.on('open', () => console.log('Connected to runtime'));

ws.on('message', (data) => {
    const msg = JSON.parse(data.toString());

    if (msg.type === 'preview_ready') {
        console.log('\n=== Preview Ready ===');
        console.log('Frame:', msg.frame);

        // Open shared memory
        if (!sharedPreview.isOpen()) {
            const opened = sharedPreview.open(msg.sharedMem);
            console.log('Opened shared memory:', opened);
        }

        // Read header
        const header = sharedPreview.getHeader();
        console.log('Header:', header);

        // Read each slot
        for (const slotInfo of msg.slots) {
            if (!slotInfo.updated) continue;

            const slot = sharedPreview.getSlot(slotInfo.slot);
            if (slot && slot.ready) {
                console.log('\nSlot', slotInfo.slot + ':');
                console.log('  Operator:', slot.operatorId);
                console.log('  Source line:', slot.sourceLine);
                console.log('  Dimensions:', slot.width, 'x', slot.height);
                console.log('  Kind:', slot.kind);
                console.log('  Has pixels:', !!slot.pixels);
                if (slot.pixels) {
                    console.log('  Pixel buffer size:', slot.pixels.length);
                    const r = slot.pixels[0], g = slot.pixels[1], b = slot.pixels[2];
                    console.log('  First pixel RGB:', r, g, b);
                    let minVal = 255, maxVal = 0;
                    for (let i = 0; i < Math.min(1000, slot.pixels.length); i++) {
                        minVal = Math.min(minVal, slot.pixels[i]);
                        maxVal = Math.max(maxVal, slot.pixels[i]);
                    }
                    console.log('  Pixel value range:', minVal, '-', maxVal);
                }
            }
        }

        sharedPreview.close();
        ws.close();
        process.exit(0);
    }
});

ws.on('error', (e) => console.error('WS Error:', e.message));
setTimeout(() => { console.log('Timeout'); process.exit(1); }, 5000);
