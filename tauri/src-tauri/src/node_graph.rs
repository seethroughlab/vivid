//! Vivid node graph using egui_node_graph2
//!
//! Displays operators as nodes with connections and texture previews.

use egui_node_graph2::*;
use std::borrow::Cow;
use std::collections::HashMap;

// =============================================================================
// Node Graph Types
// =============================================================================

/// User data attached to each node
#[derive(Debug, Clone, Default)]
pub struct VividNodeData {
    /// The operator type name (e.g., "Noise", "Blur")
    pub operator_type: String,
    /// Optional texture preview (egui texture ID)
    pub preview_texture: Option<egui::TextureId>,
    /// Preview texture size
    pub preview_size: (u32, u32),
}

impl VividNodeData {
    /// Get a color associated with an operator type for placeholder previews
    fn operator_color(operator_type: &str) -> egui::Color32 {
        match operator_type.to_lowercase().as_str() {
            // Generators - warm colors
            "noise" => egui::Color32::from_rgb(180, 100, 40),
            "gradient" => egui::Color32::from_rgb(100, 150, 200),
            "solid" | "color" => egui::Color32::from_rgb(80, 120, 180),
            "checker" | "checkerboard" => egui::Color32::from_rgb(100, 100, 100),
            "text" => egui::Color32::from_rgb(200, 200, 200),

            // Effects - cool colors
            "blur" => egui::Color32::from_rgb(60, 100, 160),
            "sharpen" => egui::Color32::from_rgb(160, 160, 60),
            "edge" | "edges" => egui::Color32::from_rgb(140, 80, 160),
            "distort" | "warp" => egui::Color32::from_rgb(160, 60, 100),

            // Compositing - neutral
            "blend" | "mix" => egui::Color32::from_rgb(120, 120, 120),
            "mask" => egui::Color32::from_rgb(80, 80, 80),
            "transform" => egui::Color32::from_rgb(100, 140, 100),

            // I/O - distinct colors
            "video" | "videoplayer" => egui::Color32::from_rgb(200, 80, 80),
            "image" | "texture" => egui::Color32::from_rgb(80, 160, 80),
            "camera" => egui::Color32::from_rgb(200, 160, 60),

            // Default
            _ => egui::Color32::from_rgb(80, 80, 100),
        }
    }
}

/// Data type for connections between nodes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VividDataType {
    Texture,
    Value,
    Audio,
    Geometry,
    Camera,
    Light,
    Event,
}

/// Value that can flow through connections
#[derive(Debug, Clone)]
pub enum VividValueType {
    Texture,
    Value(f32),
    Audio,
    Geometry,
    Camera,
    Light,
    Event,
}

impl Default for VividValueType {
    fn default() -> Self {
        VividValueType::Texture
    }
}

/// Response from node interactions
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VividResponse {
    /// A node was selected
    NodeSelected(NodeId),
    /// A node was deselected
    NodeDeselected,
    /// Solo mode requested for a node
    SoloNode(NodeId),
    /// Exit solo mode
    ExitSolo,
}

/// Graph-level state
#[derive(Default)]
pub struct VividGraphState {
    /// Currently selected node
    pub selected_node: Option<NodeId>,
    /// Node in solo mode (only this node's output is shown)
    pub solo_node: Option<NodeId>,
    /// Mapping from operator names to node IDs
    pub operator_to_node: HashMap<String, NodeId>,
}

// =============================================================================
// Trait Implementations for egui_node_graph2
// =============================================================================

impl DataTypeTrait<VividGraphState> for VividDataType {
    fn data_type_color(&self, _user_state: &mut VividGraphState) -> egui::Color32 {
        match self {
            VividDataType::Texture => egui::Color32::from_rgb(255, 128, 0),   // Orange
            VividDataType::Value => egui::Color32::from_rgb(100, 200, 255),   // Light blue
            VividDataType::Audio => egui::Color32::from_rgb(100, 255, 100),   // Green
            VividDataType::Geometry => egui::Color32::from_rgb(200, 100, 255), // Purple
            VividDataType::Camera => egui::Color32::from_rgb(255, 255, 100),  // Yellow
            VividDataType::Light => egui::Color32::from_rgb(255, 255, 200),   // Light yellow
            VividDataType::Event => egui::Color32::from_rgb(255, 100, 100),   // Red
        }
    }

    fn name(&self) -> Cow<'_, str> {
        match self {
            VividDataType::Texture => "Texture".into(),
            VividDataType::Value => "Value".into(),
            VividDataType::Audio => "Audio".into(),
            VividDataType::Geometry => "Geometry".into(),
            VividDataType::Camera => "Camera".into(),
            VividDataType::Light => "Light".into(),
            VividDataType::Event => "Event".into(),
        }
    }
}

impl NodeTemplateTrait for VividNodeData {
    type NodeData = VividNodeData;
    type DataType = VividDataType;
    type ValueType = VividValueType;
    type UserState = VividGraphState;
    type CategoryType = &'static str;

    fn node_finder_label(&self, _user_state: &mut Self::UserState) -> Cow<'_, str> {
        Cow::Borrowed(&self.operator_type)
    }

    fn node_finder_categories(&self, _user_state: &mut Self::UserState) -> Vec<&'static str> {
        vec!["Operators"]
    }

    fn node_graph_label(&self, _user_state: &mut Self::UserState) -> String {
        self.operator_type.clone()
    }

    fn user_data(&self, _user_state: &mut Self::UserState) -> Self::NodeData {
        self.clone()
    }

    fn build_node(
        &self,
        graph: &mut Graph<Self::NodeData, Self::DataType, Self::ValueType>,
        _user_state: &mut Self::UserState,
        node_id: NodeId,
    ) {
        // Add default texture input and output
        // The actual inputs/outputs will be set when syncing with vivid
        graph.add_input_param(
            node_id,
            "input".to_string(),
            VividDataType::Texture,
            VividValueType::Texture,
            InputParamKind::ConnectionOnly,
            true,
        );

        graph.add_output_param(node_id, "output".to_string(), VividDataType::Texture);
    }
}

impl WidgetValueTrait for VividValueType {
    type Response = VividResponse;
    type UserState = VividGraphState;
    type NodeData = VividNodeData;

    fn value_widget(
        &mut self,
        _param_name: &str,
        _node_id: NodeId,
        ui: &mut egui::Ui,
        _user_state: &mut Self::UserState,
        _node_data: &Self::NodeData,
    ) -> Vec<Self::Response> {
        ui.label("(connected)");
        vec![]
    }
}

impl UserResponseTrait for VividResponse {}

impl NodeDataTrait for VividNodeData {
    type Response = VividResponse;
    type UserState = VividGraphState;
    type DataType = VividDataType;
    type ValueType = VividValueType;

    fn bottom_ui(
        &self,
        ui: &mut egui::Ui,
        node_id: NodeId,
        _graph: &Graph<Self, Self::DataType, Self::ValueType>,
        user_state: &mut Self::UserState,
    ) -> Vec<NodeResponse<Self::Response, Self>> {
        let mut responses = vec![];

        // Draw texture preview or placeholder
        let preview_width = 128.0;
        let preview_height = 80.0;

        if let Some(tex_id) = self.preview_texture {
            let aspect = self.preview_size.1 as f32 / self.preview_size.0.max(1) as f32;
            let actual_height = preview_width * aspect;

            ui.add(egui::Image::new(egui::load::SizedTexture::new(
                tex_id,
                egui::vec2(preview_width, actual_height),
            )));
        } else {
            // Draw a colored placeholder based on operator type
            let color = Self::operator_color(&self.operator_type);
            let (rect, _response) = ui.allocate_exact_size(
                egui::vec2(preview_width, preview_height),
                egui::Sense::hover(),
            );

            // Draw gradient background to simulate texture
            let painter = ui.painter();
            painter.rect_filled(rect, 4.0, color);

            // Add a subtle pattern/gradient
            let lighter = egui::Color32::from_rgba_unmultiplied(
                (color.r() as u16 + 40).min(255) as u8,
                (color.g() as u16 + 40).min(255) as u8,
                (color.b() as u16 + 40).min(255) as u8,
                color.a(),
            );
            painter.rect_stroke(rect, 4.0, egui::Stroke::new(2.0, lighter));

            // Draw operator type text in center
            painter.text(
                rect.center(),
                egui::Align2::CENTER_CENTER,
                &self.operator_type,
                egui::FontId::proportional(12.0),
                egui::Color32::WHITE,
            );
        }

        // Solo button
        ui.horizontal(|ui| {
            let is_solo = user_state.solo_node == Some(node_id);
            let solo_text = if is_solo { "👁 Solo (on)" } else { "👁 Solo" };

            if ui.button(solo_text).clicked() {
                if is_solo {
                    responses.push(NodeResponse::User(VividResponse::ExitSolo));
                } else {
                    responses.push(NodeResponse::User(VividResponse::SoloNode(node_id)));
                }
            }
        });

        responses
    }
}

// =============================================================================
// Node Graph Wrapper
// =============================================================================

/// The main node graph state wrapper
pub struct VividNodeGraph {
    pub state: GraphEditorState<VividNodeData, VividDataType, VividValueType, VividNodeData, VividGraphState>,
    pub user_state: VividGraphState,
}

impl VividNodeGraph {
    /// Create a new empty node graph
    pub fn new() -> Self {
        Self {
            state: GraphEditorState::default(),
            user_state: VividGraphState::default(),
        }
    }

    /// Add a node for an operator
    pub fn add_operator(&mut self, name: &str, operator_type: &str, position: egui::Pos2) -> NodeId {
        let template = VividNodeData {
            operator_type: operator_type.to_string(),
            preview_texture: None,
            preview_size: (1, 1),
        };

        let node_id = self.state.graph.add_node(
            name.to_string(),
            template.user_data(&mut self.user_state),
            |graph, node_id| {
                // Add default input/output
                graph.add_input_param(
                    node_id,
                    "input".to_string(),
                    VividDataType::Texture,
                    VividValueType::Texture,
                    InputParamKind::ConnectionOnly,
                    true,
                );
                graph.add_output_param(node_id, "output".to_string(), VividDataType::Texture);
            },
        );

        // Set position
        self.state.node_positions.insert(node_id, position);

        // Add to node_order (required by egui_node_graph2)
        self.state.node_order.push(node_id);

        // Track mapping
        self.user_state.operator_to_node.insert(name.to_string(), node_id);

        node_id
    }

    /// Connect two nodes
    pub fn connect(&mut self, from_node: NodeId, to_node: NodeId) {
        // Find output param of from_node
        if let Some(from_node_data) = self.state.graph.nodes.get(from_node) {
            if let Some((_, output_id)) = from_node_data.outputs.first() {
                // Find input param of to_node
                if let Some(to_node_data) = self.state.graph.nodes.get(to_node) {
                    if let Some((_, input_id)) = to_node_data.inputs.first() {
                        // Third arg is position in the input's connection list
                        self.state.graph.add_connection(*output_id, *input_id, 0);
                    }
                }
            }
        }
    }

    /// Set preview texture for a node
    pub fn set_preview_texture(&mut self, node_id: NodeId, texture_id: egui::TextureId, width: u32, height: u32) {
        if let Some(node) = self.state.graph.nodes.get_mut(node_id) {
            node.user_data.preview_texture = Some(texture_id);
            node.user_data.preview_size = (width, height);
        }
    }

    /// Clear the graph
    pub fn clear(&mut self) {
        self.state = GraphEditorState::default();
        self.user_state = VividGraphState::default();
    }

    /// Draw the node graph
    pub fn draw(&mut self, ctx: &egui::Context) -> Vec<VividResponse> {
        let mut responses = vec![];

        egui::CentralPanel::default()
            .frame(egui::Frame::none().fill(egui::Color32::from_rgba_unmultiplied(20, 20, 25, 255)))
            .show(ctx, |ui| {
                let graph_response = self.state.draw_graph_editor(
                    ui,
                    AllNodeTemplates(vec![VividNodeData::default()]),
                    &mut self.user_state,
                    vec![], // No categories for now
                );

                // Process responses
                for response in graph_response.node_responses {
                    match response {
                        NodeResponse::User(user_response) => {
                            match user_response {
                                VividResponse::SoloNode(node_id) => {
                                    self.user_state.solo_node = Some(node_id);
                                    responses.push(user_response);
                                }
                                VividResponse::ExitSolo => {
                                    self.user_state.solo_node = None;
                                    responses.push(user_response);
                                }
                                _ => responses.push(user_response),
                            }
                        }
                        NodeResponse::SelectNode(node_id) => {
                            self.user_state.selected_node = Some(node_id);
                            responses.push(VividResponse::NodeSelected(node_id));
                        }
                        _ => {}
                    }
                }
            });

        responses
    }

    /// Get node ID for an operator name
    pub fn get_node_id(&self, operator_name: &str) -> Option<NodeId> {
        self.user_state.operator_to_node.get(operator_name).copied()
    }
}

impl Default for VividNodeGraph {
    fn default() -> Self {
        Self::new()
    }
}

/// All available node templates
struct AllNodeTemplates(Vec<VividNodeData>);

impl NodeTemplateIter for AllNodeTemplates {
    type Item = VividNodeData;

    fn all_kinds(&self) -> Vec<Self::Item> {
        self.0.clone()
    }
}
