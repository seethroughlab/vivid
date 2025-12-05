const WebSocket = require('ws');

console.log('Connecting to ws://localhost:9876...');

const ws = new WebSocket('ws://localhost:9876');

ws.on('open', () => {
    console.log('✓ Connected to Vivid runtime!');

    // Send a reload command
    const msg = JSON.stringify({ type: 'reload' });
    ws.send(msg);
    console.log('✓ Sent reload command');
});

ws.on('message', (data) => {
    try {
        const msg = JSON.parse(data.toString());
        console.log('✓ Received message type:', msg.type);
        if (msg.type === 'compile_status') {
            console.log('  Success:', msg.success);
            console.log('  Message:', msg.message);
        }
    } catch (e) {
        console.log('  Raw data:', data.toString().substring(0, 200));
    }
});

ws.on('error', (err) => {
    console.error('✗ Error:', err.message || err);
});

ws.on('close', (code, reason) => {
    console.log('Connection closed:', code, reason ? reason.toString() : '');
});

setTimeout(() => {
    console.log('Test timeout - closing connection');
    ws.close();
    process.exit(0);
}, 4000);
