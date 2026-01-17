use std::sync::Mutex;
use tauri::WebviewWindow;
use wgpu::util::DeviceExt;

use crate::egui_state::EguiState;
use crate::node_graph::VividNodeGraph;

/// Uniform buffer for time-based animation
/// WGSL alignment: vec2 needs 8-byte alignment
/// Total: 24 bytes = time(4) + pad(4) + resolution(8) + pad(8)
#[repr(C)]
#[derive(Debug, Copy, Clone, bytemuck::Pod, bytemuck::Zeroable)]
struct Uniforms {
    time: f32,            // offset 0, size 4
    _pad1: f32,           // offset 4, size 4 - align vec2 to 8
    resolution: [f32; 2], // offset 8, size 8
    _pad2: [f32; 2],      // offset 16, size 8 - total 24 bytes
}

pub struct WgpuState {
    pub device: wgpu::Device,
    pub queue: wgpu::Queue,
    pub surface: wgpu::Surface<'static>,
    pub render_pipeline: wgpu::RenderPipeline,
    pub config: Mutex<wgpu::SurfaceConfiguration>,
    pub uniform_buffer: wgpu::Buffer,
    pub uniform_bind_group: wgpu::BindGroup,
    // egui state
    pub egui_state: Mutex<EguiState>,
    pub node_graph: Mutex<VividNodeGraph>,
    // Whether to show the node graph
    pub show_node_graph: Mutex<bool>,
}

impl WgpuState {
    pub async fn new(window: &WebviewWindow) -> Self {
        let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
            backends: wgpu::Backends::all(),
            ..Default::default()
        });

        // Get the raw window handle for surface creation
        // We need to use unsafe because the surface requires a 'static lifetime
        let tauri_window = window.as_ref().window();
        let size = tauri_window.inner_size().expect("Failed to get window size");

        // SAFETY: The window will live for the lifetime of the application
        // This is a common pattern for wgpu+Tauri integration
        let surface = unsafe {
            instance.create_surface_unsafe(
                wgpu::SurfaceTargetUnsafe::from_window(&tauri_window)
                    .expect("Failed to create surface target")
            ).expect("Failed to create surface")
        };

        let adapter = instance
            .request_adapter(&wgpu::RequestAdapterOptions {
                power_preference: wgpu::PowerPreference::HighPerformance,
                compatible_surface: Some(&surface),
                force_fallback_adapter: false,
            })
            .await
            .expect("Failed to find an appropriate adapter");

        let (device, queue) = adapter
            .request_device(
                &wgpu::DeviceDescriptor {
                    label: Some("Vivid Device"),
                    required_features: wgpu::Features::empty(),
                    required_limits: wgpu::Limits::default(),
                    memory_hints: Default::default(),
                },
                None,
            )
            .await
            .expect("Failed to create device");

        // Create the shader module
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Noise Shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("shader.wgsl").into()),
        });

        // Create uniform buffer
        let uniforms = Uniforms {
            time: 0.0,
            _pad1: 0.0,
            resolution: [1280.0, 800.0],
            _pad2: [0.0, 0.0],
        };

        let uniform_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some("Uniform Buffer"),
            contents: bytemuck::cast_slice(&[uniforms]),
            usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
        });

        // Create bind group layout
        let bind_group_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("Uniform Bind Group Layout"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: false,
                    min_binding_size: None,
                },
                count: None,
            }],
        });

        let uniform_bind_group = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("Uniform Bind Group"),
            layout: &bind_group_layout,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                resource: uniform_buffer.as_entire_binding(),
            }],
        });

        // Create pipeline layout
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("Render Pipeline Layout"),
            bind_group_layouts: &[&bind_group_layout],
            push_constant_ranges: &[],
        });

        // Get surface capabilities and configure
        let surface_caps = surface.get_capabilities(&adapter);
        let surface_format = surface_caps
            .formats
            .iter()
            .copied()
            .find(|f| f.is_srgb())
            .unwrap_or(surface_caps.formats[0]);

        // Choose the best available alpha mode for transparency
        let alpha_mode = surface_caps
            .alpha_modes
            .iter()
            .copied()
            .find(|m| *m == wgpu::CompositeAlphaMode::PostMultiplied)
            .unwrap_or(surface_caps.alpha_modes[0]);

        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface_format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::Fifo,
            alpha_mode,
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        // Create render pipeline
        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Render Pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: "vs_main",
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: "fs_main",
                targets: &[Some(wgpu::ColorTargetState {
                    format: surface_format,
                    blend: Some(wgpu::BlendState::PREMULTIPLIED_ALPHA_BLENDING),
                    write_mask: wgpu::ColorWrites::ALL,
                })],
                compilation_options: Default::default(),
            }),
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                strip_index_format: None,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: None,
                polygon_mode: wgpu::PolygonMode::Fill,
                unclipped_depth: false,
                conservative: false,
            },
            depth_stencil: None,
            multisample: wgpu::MultisampleState::default(),
            multiview: None,
            cache: None,
        });

        // Get scale factor for HiDPI support
        let scale_factor = tauri_window.scale_factor().expect("Failed to get scale factor") as f32;

        // Create egui state with correct scale factor
        let mut egui_state = EguiState::new(&device, surface_format, size.width.max(1), size.height.max(1));
        egui_state.set_scale_factor(scale_factor);

        // Create node graph with a demo node
        let mut node_graph = VividNodeGraph::new();
        node_graph.add_operator("noise", "Noise", egui::pos2(100.0, 100.0));
        node_graph.add_operator("blur", "Blur", egui::pos2(350.0, 100.0));

        // Connect noise -> blur
        if let (Some(noise_id), Some(blur_id)) = (
            node_graph.get_node_id("noise"),
            node_graph.get_node_id("blur"),
        ) {
            node_graph.connect(noise_id, blur_id);
        }

        Self {
            device,
            queue,
            surface,
            render_pipeline,
            config: Mutex::new(config),
            uniform_buffer,
            uniform_bind_group,
            egui_state: Mutex::new(egui_state),
            node_graph: Mutex::new(node_graph),
            show_node_graph: Mutex::new(true),
        }
    }

    pub fn resize(&self, width: u32, height: u32, scale_factor: Option<f32>) {
        let mut config = self.config.lock().unwrap();
        config.width = width.max(1);
        config.height = height.max(1);
        self.surface.configure(&self.device, &config);

        // Resize egui and optionally update scale factor
        let mut egui_state = self.egui_state.lock().unwrap();
        egui_state.resize(width.max(1), height.max(1));
        if let Some(scale) = scale_factor {
            egui_state.set_scale_factor(scale);
        }
    }

    pub fn render(&self, time: f32) -> Result<(), wgpu::SurfaceError> {
        let config = self.config.lock().unwrap();

        // Update uniforms
        let uniforms = Uniforms {
            time,
            _pad1: 0.0,
            resolution: [config.width as f32, config.height as f32],
            _pad2: [0.0, 0.0],
        };
        self.queue.write_buffer(&self.uniform_buffer, 0, bytemuck::cast_slice(&[uniforms]));

        drop(config);

        let output = self.surface.get_current_texture()?;
        let view = output.texture.create_view(&wgpu::TextureViewDescriptor::default());

        let mut encoder = self
            .device
            .create_command_encoder(&wgpu::CommandEncoderDescriptor {
                label: Some("Render Encoder"),
            });

        // First render pass: Background shader (DISABLED for debugging)
        {
            let mut render_pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("Background Render Pass"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        // Clear to dark gray instead of running noise shader
                        load: wgpu::LoadOp::Clear(wgpu::Color {
                            r: 0.1,
                            g: 0.1,
                            b: 0.12,
                            a: 1.0,
                        }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });

            render_pass.set_pipeline(&self.render_pipeline);
            render_pass.set_bind_group(0, &self.uniform_bind_group, &[]);
            render_pass.draw(0..6, 0..1);
        }

        // Render egui (node graph)
        let show_node_graph = *self.show_node_graph.lock().unwrap();
        let egui_commands = if show_node_graph {
            let egui_state = self.egui_state.lock().unwrap();
            let mut node_graph = self.node_graph.lock().unwrap();

            // Begin egui frame
            let ctx = egui_state.begin_frame();

            // Draw node graph
            let _responses = node_graph.draw(ctx);

            // End frame and render - returns command buffer
            Some(egui_state.end_frame_and_render(&self.device, &self.queue, &view))
        } else {
            None
        };

        // Submit all command buffers
        let mut commands = vec![encoder.finish()];
        if let Some(egui_cmd) = egui_commands {
            commands.push(egui_cmd);
        }
        self.queue.submit(commands);
        output.present();

        Ok(())
    }

    /// Toggle node graph visibility
    pub fn toggle_node_graph(&self) {
        let mut show = self.show_node_graph.lock().unwrap();
        *show = !*show;
    }

    /// Set node graph visibility
    pub fn set_show_node_graph(&self, show: bool) {
        *self.show_node_graph.lock().unwrap() = show;
    }

    // Input forwarding to egui

    /// Forward mouse position to egui
    pub fn inject_mouse_pos(&self, x: f32, y: f32) {
        let egui_state = self.egui_state.lock().unwrap();
        egui_state.inject_mouse_pos(x, y);
    }

    /// Forward mouse button to egui
    pub fn inject_mouse_button(&self, button: u32, pressed: bool) {
        let egui_state = self.egui_state.lock().unwrap();
        egui_state.inject_mouse_button(button, pressed);
    }

    /// Forward scroll to egui
    pub fn inject_scroll(&self, dx: f32, dy: f32) {
        let egui_state = self.egui_state.lock().unwrap();
        egui_state.inject_scroll(dx, dy);
    }
}
