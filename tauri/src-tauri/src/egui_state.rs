//! egui integration for the Tauri app
//!
//! Provides egui rendering on top of the wgpu surface for native UI elements
//! like the node graph.

use egui::Context as EguiContext;
use egui_wgpu::Renderer as EguiRenderer;
use egui_wgpu::ScreenDescriptor;
use std::sync::Mutex;
use wgpu::{Device, Queue, TextureFormat, TextureView};

/// Helper function to render egui with proper lifetime handling.
/// egui-wgpu 0.29 requires RenderPass<'static>, but we can safely use
/// transmute because the render pass is fully consumed within this function.
fn render_egui(
    encoder: &mut wgpu::CommandEncoder,
    view: &TextureView,
    renderer: &mut EguiRenderer,
    paint_jobs: &[egui::ClippedPrimitive],
    screen_descriptor: &ScreenDescriptor,
) {
    let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
        label: Some("egui Render Pass"),
        color_attachments: &[Some(wgpu::RenderPassColorAttachment {
            view,
            resolve_target: None,
            ops: wgpu::Operations {
                load: wgpu::LoadOp::Load,
                store: wgpu::StoreOp::Store,
            },
        })],
        depth_stencil_attachment: None,
        timestamp_writes: None,
        occlusion_query_set: None,
    });

    // SAFETY: The render pass is fully consumed within this function before
    // the encoder is used again. egui-wgpu 0.29 requires 'static but this is
    // a known limitation of that version.
    let render_pass_static: &mut wgpu::RenderPass<'static> =
        unsafe { std::mem::transmute(&mut render_pass) };

    renderer.render(render_pass_static, paint_jobs, screen_descriptor);
}

/// Manages egui state and rendering
pub struct EguiState {
    /// egui context
    pub context: EguiContext,
    /// egui-wgpu renderer
    renderer: Mutex<EguiRenderer>,
    /// Screen scale factor
    scale_factor: f32,
    /// Current screen size
    screen_size: (u32, u32),
    /// Last known mouse position (for button events)
    last_mouse_pos: Mutex<egui::Pos2>,
    /// Accumulated events to be processed next frame
    pending_events: Mutex<Vec<egui::Event>>,
}

impl EguiState {
    /// Create a new egui state
    pub fn new(device: &Device, surface_format: TextureFormat, width: u32, height: u32) -> Self {
        let context = EguiContext::default();

        // Configure egui style for dark theme
        let mut style = (*context.style()).clone();
        style.visuals = egui::Visuals::dark();

        // Make the background more transparent for overlay effect
        style.visuals.window_fill = egui::Color32::from_rgba_unmultiplied(30, 30, 30, 240);
        style.visuals.panel_fill = egui::Color32::from_rgba_unmultiplied(30, 30, 30, 240);

        context.set_style(style);

        // Create egui-wgpu renderer
        let renderer = EguiRenderer::new(device, surface_format, None, 1, false);

        Self {
            context,
            renderer: Mutex::new(renderer),
            scale_factor: 1.0,
            screen_size: (width, height),
            last_mouse_pos: Mutex::new(egui::Pos2::ZERO),
            pending_events: Mutex::new(Vec::new()),
        }
    }

    /// Update screen size
    pub fn resize(&mut self, width: u32, height: u32) {
        self.screen_size = (width, height);
    }

    /// Set scale factor (for HiDPI displays)
    pub fn set_scale_factor(&mut self, scale: f32) {
        self.scale_factor = scale;
    }

    /// Get current scale factor
    pub fn get_scale_factor(&self) -> f32 {
        self.scale_factor
    }

    /// Begin a new frame
    ///
    /// Returns the egui context for building UI
    pub fn begin_frame(&self) -> &EguiContext {
        // Take pending events
        let events = std::mem::take(&mut *self.pending_events.lock().unwrap());
        let mouse_pos = *self.last_mouse_pos.lock().unwrap();

        let mut raw_input = egui::RawInput::default();
        raw_input.screen_rect = Some(egui::Rect::from_min_size(
            egui::Pos2::ZERO,
            egui::vec2(
                self.screen_size.0 as f32 / self.scale_factor,
                self.screen_size.1 as f32 / self.scale_factor,
            ),
        ));
        raw_input.events = events;

        // Also set the current pointer position so egui knows where the mouse is
        // even if there's no PointerMoved event this frame
        if mouse_pos != egui::Pos2::ZERO {
            raw_input.events.insert(0, egui::Event::PointerMoved(mouse_pos));
        }

        self.context.begin_pass(raw_input);
        &self.context
    }

    /// End the frame and render
    /// Returns a command buffer that should be submitted to the queue
    pub fn end_frame_and_render(
        &self,
        device: &Device,
        queue: &Queue,
        view: &TextureView,
    ) -> wgpu::CommandBuffer {
        let full_output = self.context.end_pass();
        let paint_jobs = self.context.tessellate(full_output.shapes, self.scale_factor);

        let screen_descriptor = ScreenDescriptor {
            size_in_pixels: [self.screen_size.0, self.screen_size.1],
            pixels_per_point: self.scale_factor,
        };

        let mut renderer = self.renderer.lock().unwrap();

        // Update textures
        for (id, delta) in &full_output.textures_delta.set {
            renderer.update_texture(device, queue, *id, delta);
        }

        // Create our own encoder for egui rendering
        let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
            label: Some("egui Encoder"),
        });

        // Update buffers
        renderer.update_buffers(device, queue, &mut encoder, &paint_jobs, &screen_descriptor);

        // Render using the 'static lifetime pattern required by egui-wgpu 0.29
        // This is safe because the render pass is dropped before encoder.finish()
        render_egui(
            &mut encoder,
            view,
            &mut *renderer,
            &paint_jobs,
            &screen_descriptor,
        );

        // Free textures
        for id in &full_output.textures_delta.free {
            renderer.free_texture(id);
        }

        encoder.finish()
    }

    /// Inject a mouse position event
    /// Note: x, y are expected to be in logical (CSS) pixels from the webview,
    /// which matches egui's coordinate system. No scaling needed.
    pub fn inject_mouse_pos(&self, x: f32, y: f32) {
        let pos = egui::pos2(x, y);

        // Store the position for button events
        *self.last_mouse_pos.lock().unwrap() = pos;

        // Queue the event for next frame
        self.pending_events.lock().unwrap().push(egui::Event::PointerMoved(pos));
    }

    /// Inject a mouse button event
    pub fn inject_mouse_button(&self, button: u32, pressed: bool) {
        let egui_button = match button {
            0 => egui::PointerButton::Primary,
            1 => egui::PointerButton::Secondary,
            2 => egui::PointerButton::Middle,
            _ => return,
        };

        let pos = *self.last_mouse_pos.lock().unwrap();

        // Queue the event for next frame
        self.pending_events.lock().unwrap().push(egui::Event::PointerButton {
            pos,
            button: egui_button,
            pressed,
            modifiers: egui::Modifiers::default(),
        });
    }

    /// Inject a scroll event
    pub fn inject_scroll(&self, dx: f32, dy: f32) {
        // Queue the event for next frame
        self.pending_events.lock().unwrap().push(egui::Event::MouseWheel {
            unit: egui::MouseWheelUnit::Point,
            delta: egui::vec2(-dx, -dy),  // Invert for natural scrolling
            modifiers: egui::Modifiers::default(),
        });
    }

    /// Check if egui wants the mouse input
    pub fn wants_mouse(&self) -> bool {
        self.context.wants_pointer_input()
    }

    /// Check if egui wants keyboard input
    pub fn wants_keyboard(&self) -> bool {
        self.context.wants_keyboard_input()
    }
}
