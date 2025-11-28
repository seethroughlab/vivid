#!/usr/bin/env node
/**
 * Auto-generate operator reference documentation from source code
 *
 * Usage: node scripts/generate-operator-docs.js > docs/OPERATORS.md
 */

const fs = require('fs');
const path = require('path');

const operatorsDir = path.join(__dirname, '..', 'operators');

// Parse a single operator file
function parseOperator(filePath) {
    const content = fs.readFileSync(filePath, 'utf8');
    const fileName = path.basename(filePath, '.cpp');

    const op = {
        name: '',
        description: '',
        params: [],
        outputKind: 'Texture',
        fluentMethods: [],
        fileName
    };

    // Extract description from comments at top
    const descMatch = content.match(/^\/\/\s*(.+)\n\/\/\s*(.+)/m);
    if (descMatch) {
        op.name = descMatch[1].replace(' Operator', '').trim();
        op.description = descMatch[2].trim();
    }

    // Extract class name
    const classMatch = content.match(/class\s+(\w+)\s*:\s*public\s+Operator/);
    if (classMatch) {
        op.name = classMatch[1];
    }

    // Extract params from params() override
    const paramsMatch = content.match(/std::vector<ParamDecl>\s+params\(\)\s+override\s*\{[\s\S]*?return\s*\{([\s\S]*?)\};/);
    if (paramsMatch) {
        const paramsContent = paramsMatch[1];

        // Match floatParam, intParam, boolParam, etc.
        const paramRegex = /(\w+Param)\s*\(\s*"(\w+)"\s*,\s*[\w_.]+\s*(?:,\s*([\d.f-]+)\s*,\s*([\d.f-]+))?\s*\)/g;
        let match;
        while ((match = paramRegex.exec(paramsContent)) !== null) {
            const param = {
                name: match[2],
                type: match[1].replace('Param', ''),
                min: match[3] ? match[3].replace('f', '') : null,
                max: match[4] ? match[4].replace('f', '') : null
            };
            op.params.push(param);
        }
    }

    // Extract output kind
    const outputMatch = content.match(/OutputKind\s+outputKind\(\)[\s\S]*?return\s+OutputKind::(\w+)/);
    if (outputMatch) {
        op.outputKind = outputMatch[1];
    }

    // Extract fluent methods
    const fluentRegex = /(\w+)&\s+(\w+)\s*\([^)]*\)\s*\{[^}]*return\s+\*this/g;
    let fluentMatch;
    while ((fluentMatch = fluentRegex.exec(content)) !== null) {
        if (fluentMatch[2] !== 'input') { // Skip generic input method
            op.fluentMethods.push(fluentMatch[2]);
        }
    }

    // Extract default values from private members
    const defaultsRegex = /(\w+)_\s*=\s*([\d.f-]+|true|false|"[^"]*")/g;
    let defaultMatch;
    const defaults = {};
    while ((defaultMatch = defaultsRegex.exec(content)) !== null) {
        defaults[defaultMatch[1]] = defaultMatch[2].replace('f', '').replace(/"/g, '');
    }

    // Add defaults to params
    op.params.forEach(param => {
        if (defaults[param.name]) {
            param.default = defaults[param.name];
        }
    });

    return op;
}

// Generate markdown for an operator
function generateMarkdown(op) {
    let md = `### ${op.name}\n\n`;
    md += `${op.description}\n\n`;

    if (op.params.length > 0) {
        md += `**Parameters:**\n\n`;
        md += `| Parameter | Type | Default | Range |\n`;
        md += `|-----------|------|---------|-------|\n`;
        op.params.forEach(param => {
            const range = (param.min && param.max) ? `${param.min} - ${param.max}` : '-';
            const def = param.default || '-';
            md += `| ${param.name} | ${param.type} | ${def} | ${range} |\n`;
        });
        md += '\n';
    }

    md += `**Output:** ${op.outputKind}\n\n`;

    if (op.fluentMethods.length > 0) {
        md += `**Fluent Methods:** ${op.fluentMethods.map(m => '`.' + m + '()`').join(', ')}\n\n`;
    }

    md += `---\n\n`;
    return md;
}

// Main
function main() {
    const files = fs.readdirSync(operatorsDir)
        .filter(f => f.endsWith('.cpp'))
        .sort();

    let output = `# Built-in Operator Reference

This reference is auto-generated from the operator source files.

## Texture Operators (TOPs)

`;

    const textureOps = [];
    const valueOps = [];

    files.forEach(file => {
        const filePath = path.join(operatorsDir, file);
        const op = parseOperator(filePath);

        if (op.outputKind === 'Value' || op.outputKind === 'ValueArray') {
            valueOps.push(op);
        } else {
            textureOps.push(op);
        }
    });

    textureOps.forEach(op => {
        output += generateMarkdown(op);
    });

    if (valueOps.length > 0) {
        output += `## Channel Operators (CHOPs)\n\n`;
        valueOps.forEach(op => {
            output += generateMarkdown(op);
        });
    }

    output += `## Usage Example

\`\`\`cpp
#include <vivid/vivid.h>

class MyPipeline : public vivid::Operator {
    vivid::Texture noise_, blurred_, output_;

public:
    void init(vivid::Context& ctx) override {
        noise_ = ctx.createTexture();
        blurred_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(vivid::Context& ctx) override {
        // Generate noise
        vivid::Context::ShaderParams noiseParams;
        noiseParams.param0 = 4.0f;  // scale
        noiseParams.param1 = ctx.time();
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Apply blur
        vivid::Context::ShaderParams blurParams;
        blurParams.param0 = 5.0f;  // radius
        ctx.runShader("shaders/blur.wgsl", &noise_, blurred_, blurParams);

        // Adjust brightness
        vivid::Context::ShaderParams brightParams;
        brightParams.param0 = 0.2f;  // brightness
        brightParams.param1 = 1.2f;  // contrast
        ctx.runShader("shaders/brightness.wgsl", &blurred_, output_, brightParams);

        ctx.setOutput("out", output_);
    }

    vivid::OutputKind outputKind() override { return vivid::OutputKind::Texture; }
};
VIVID_OPERATOR(MyPipeline)
\`\`\`
`;

    console.log(output);
}

main();
