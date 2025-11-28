// Quick test of the shared preview native module

const sharedPreview = require('./build/Release/shared_preview.node');

console.log('Constants:', {
    THUMB_WIDTH: sharedPreview.THUMB_WIDTH,
    THUMB_HEIGHT: sharedPreview.THUMB_HEIGHT,
    MAX_OPERATORS: sharedPreview.MAX_OPERATORS
});

async function test() {
    console.log('Attempting to open shared memory...');
    const opened = sharedPreview.open('vivid_preview');
    console.log('Open result:', opened);

    if (opened) {
        // Poll a few times to wait for preview data
        for (let attempt = 0; attempt < 10; attempt++) {
            const header = sharedPreview.getHeader();
            console.log(`Attempt ${attempt + 1} - Header:`, header);

            // Check first slot regardless of operatorCount
            const slot = sharedPreview.getSlot(0);
            if (slot && slot.ready) {
                console.log('Slot 0:', {
                    operatorId: slot.operatorId,
                    sourceLine: slot.sourceLine,
                    kind: slot.kind,
                    width: slot.width,
                    height: slot.height,
                    hasPixels: !!slot.pixels,
                    pixelSize: slot.pixels?.length,
                    firstPixels: slot.pixels ? Array.from(slot.pixels.slice(0, 12)) : null
                });
                break;
            }

            await new Promise(r => setTimeout(r, 200));
        }

        sharedPreview.close();
        console.log('Closed shared memory');
    } else {
        console.log('Could not open shared memory (runtime not running?)');
    }
}

test();
