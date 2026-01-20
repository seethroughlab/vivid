// Shape Controls - JavaScript for Vivid WebView interaction
// This file demonstrates loading external JS files in WebView

// Send parameter update to Vivid
function sendParam(param, value) {
    if (window.vivid && window.vivid.updateParam) {
        window.vivid.updateParam(JSON.stringify({ param, value }));
    }
}

// Update color preview box
function updateColorPreview() {
    const r = Math.round(parseFloat(document.getElementById('colorR').value) * 255);
    const g = Math.round(parseFloat(document.getElementById('colorG').value) * 255);
    const b = Math.round(parseFloat(document.getElementById('colorB').value) * 255);
    document.getElementById('color-preview').style.background = `rgb(${r}, ${g}, ${b})`;
}

// Initialize controls when DOM is ready
function initControls() {
    // Set up slider event handlers
    const sliders = ['size', 'posX', 'posY', 'colorR', 'colorG', 'colorB'];

    sliders.forEach(id => {
        const slider = document.getElementById(id);
        const display = document.getElementById(id + '-value');

        if (!slider || !display) {
            console.warn(`Slider ${id} not found`);
            return;
        }

        // Update on input (while dragging)
        slider.addEventListener('input', () => {
            const value = parseFloat(slider.value);
            display.textContent = value.toFixed(2);
            sendParam(id, value);

            if (id.startsWith('color')) {
                updateColorPreview();
            }
        });
    });

    // Initialize color preview
    updateColorPreview();

    // Animation toggle
    let animating = false;
    const animateBtn = document.getElementById('animate-btn');

    if (animateBtn) {
        animateBtn.addEventListener('click', () => {
            animating = !animating;
            animateBtn.textContent = animating ? 'Stop Animation' : 'Start Animation';
            animateBtn.classList.toggle('active', animating);
            sendParam('animate', animating);
        });
    }

    // Reset button
    const resetBtn = document.getElementById('reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', () => {
            // Reset transform values
            resetSlider('size', 0.3);
            resetSlider('posX', 0);
            resetSlider('posY', 0);

            // Reset color values
            resetSlider('colorR', 1);
            resetSlider('colorG', 0.5);
            resetSlider('colorB', 0.2);

            updateColorPreview();

            // Stop animation if running
            if (animating) {
                animating = false;
                animateBtn.textContent = 'Start Animation';
                animateBtn.classList.remove('active');
                sendParam('animate', false);
            }
        });
    }

    console.log('[Controls] Initialized successfully');
}

// Helper to reset a slider to a default value
function resetSlider(id, defaultValue) {
    const slider = document.getElementById(id);
    const display = document.getElementById(id + '-value');

    if (slider && display) {
        slider.value = defaultValue;
        display.textContent = defaultValue.toFixed(2);
        sendParam(id, defaultValue);
    }
}

// Debug: show drag state
function showDebug(msg) {
    let dbg = document.getElementById('debug');
    if (!dbg) {
        dbg = document.createElement('div');
        dbg.id = 'debug';
        dbg.style.cssText = 'position:fixed;bottom:10px;left:10px;background:rgba(0,0,0,0.8);color:#0f0;padding:10px;font-family:monospace;font-size:12px;z-index:9999;max-width:400px;';
        document.body.appendChild(dbg);
    }
    dbg.textContent = msg;
}

// Listen for all mouse events to debug
document.addEventListener('mousedown', (e) => {
    showDebug(`DOWN: ${e.target.tagName} #${e.target.id} @ (${Math.round(e.clientX)}, ${Math.round(e.clientY)})`);
});
document.addEventListener('mousemove', (e) => {
    if (e.buttons > 0) {
        showDebug(`DRAG: ${e.target.tagName} @ (${Math.round(e.clientX)}, ${Math.round(e.clientY)}) buttons=${e.buttons}`);
    }
});
document.addEventListener('mouseup', (e) => {
    showDebug(`UP: ${e.target.tagName} @ (${Math.round(e.clientX)}, ${Math.round(e.clientY)})`);
});

// Initialize when DOM is loaded
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initControls);
} else {
    initControls();
}
