use std::sync::Arc;
use std::time::Instant;
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::{ActiveEventLoop, ControlFlow, EventLoop},
    window::{Window, WindowId},
};

struct App {
    window: Option<Arc<Window>>,
    surface: Option<wgpu::Surface<'static>>,
    device: Option<wgpu::Device>,
    queue: Option<wgpu::Queue>,
    config: Option<wgpu::SurfaceConfiguration>,
    render_pipeline: Option<wgpu::RenderPipeline>,
    start_time: Instant,
    frame_count: u64,
    last_report: Instant,
}

impl App {
    fn new() -> Self {
        Self {
            window: None, surface: None, device: None, queue: None,
            config: None, render_pipeline: None,
            start_time: Instant::now(), frame_count: 0, last_report: Instant::now(),
        }
    }

    fn init_wgpu(&mut self, window: Arc<Window>) {
        let instance = wgpu::Instance::new(&wgpu::InstanceDescriptor {
            backends: wgpu::Backends::METAL,
            ..Default::default()
        });

        let surface = instance.create_surface(window.clone()).unwrap();
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
        })).expect("Failed to find adapter");

        println!("Adapter: {:?}", adapter.get_info().name);
        println!("Backend: {:?}", adapter.get_info().backend);

        let (device, queue) = pollster::block_on(adapter.request_device(
            &wgpu::DeviceDescriptor::default(), None,
        )).expect("Failed to create device");

        let size = window.inner_size();
        let config = wgpu::SurfaceConfiguration {
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            format: surface.get_capabilities(&adapter).formats[0],
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::Fifo,
            alpha_mode: wgpu::CompositeAlphaMode::Auto,
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
        };
        surface.configure(&device, &config);

        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("Shader"),
            source: wgpu::ShaderSource::Wgsl(
                "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
                    var p = array<vec2f, 3>(vec2f(-1,-1), vec2f(3,-1), vec2f(-1,3));
                    return vec4f(p[i], 0, 1);
                }
                @fragment fn fs() -> @location(0) vec4f { return vec4f(0.1, 0.2, 0.3, 1); }"
                .into(),
            ),
        });

        let render_pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("Pipeline"),
            layout: None,
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs"),
                buffers: &[],
                compilation_options: Default::default(),
            },
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs"),
                targets: &[Some(config.format.into())],
                compilation_options: Default::default(),
            }),
            primitive: Default::default(),
            depth_stencil: None,
            multisample: Default::default(),
            multiview: None,
            cache: None,
        });

        self.window = Some(window);
        self.surface = Some(surface);
        self.device = Some(device);
        self.queue = Some(queue);
        self.config = Some(config);
        self.render_pipeline = Some(render_pipeline);

        println!("\nRunning... Monitor memory with: ps -o rss= -p <pid>\n");
    }

    fn render(&mut self) {
        let surface = self.surface.as_ref().unwrap();
        let device = self.device.as_ref().unwrap();
        let queue = self.queue.as_ref().unwrap();
        let pipeline = self.render_pipeline.as_ref().unwrap();

        let Ok(frame) = surface.get_current_texture() else { return };
        let view = frame.texture.create_view(&Default::default());
        let mut encoder = device.create_command_encoder(&Default::default());

        {
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: None,
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        load: wgpu::LoadOp::Clear(wgpu::Color { r: 0.1, g: 0.2, b: 0.3, a: 1.0 }),
                        store: wgpu::StoreOp::Store,
                    },
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
            });
            pass.set_pipeline(pipeline);
            pass.draw(0..3, 0..1);
        }

        queue.submit(std::iter::once(encoder.finish()));
        frame.present();
        self.frame_count += 1;

        if self.last_report.elapsed().as_secs() >= 10 {
            println!("[{:>4}s] {} frames", self.start_time.elapsed().as_secs(), self.frame_count);
            self.last_report = Instant::now();
        }
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_none() {
            let window = Arc::new(event_loop.create_window(
                Window::default_attributes()
                    .with_title("wgpu Metal Leak Repro")
                    .with_inner_size(winit::dpi::LogicalSize::new(800, 600))
            ).unwrap());
            self.init_wgpu(window);
        }
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::Resized(size) => {
                if let (Some(s), Some(d), Some(c)) = (&self.surface, &self.device, &mut self.config) {
                    c.width = size.width.max(1);
                    c.height = size.height.max(1);
                    s.configure(d, c);
                }
            }
            WindowEvent::RedrawRequested => {
                self.render();
                self.window.as_ref().unwrap().request_redraw();
            }
            _ => {}
        }
    }
}

fn main() {
    env_logger::init();
    let event_loop = EventLoop::new().unwrap();
    event_loop.set_control_flow(ControlFlow::Poll);
    event_loop.run_app(&mut App::new()).unwrap();
}
