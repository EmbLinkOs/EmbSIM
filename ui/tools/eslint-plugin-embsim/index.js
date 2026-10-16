// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//
// ESLint rules enforcing SPEC-011 §6.8 (UI-001): the workbench computes no
// simulation state and only packages/protocol talks to the outside world.

const SIMULATION_PACKAGES = [
  'ngspice',
  'spice',
  'eecircuit-engine',
  'circuitjs',
  'mathjs',
  'numeric',
  'ml-matrix',
  'algebra.js',
  'nerdamer',
  'simple-statistics',
  'verilog',
  'digitaljs',
  'yosys',
  'cxxrtl',
  'wasm-spice',
];

const IO_GLOBALS = ['WebSocket', 'fetch', 'XMLHttpRequest', 'EventSource', 'RTCPeerConnection'];

/** @type {import('eslint').Rule.RuleModule} */
const noSimulationImports = {
  meta: {
    type: 'problem',
    docs: { description: 'forbid simulation and numerical-solver libraries in the workbench (UI-001)' },
    schema: [],
    messages: { forbidden: "'{{name}}' looks like a simulation or solver library; the workbench shows what the engine reports (SPEC-011 §6.8)." },
  },
  create(context) {
    const check = (node, source) => {
      if (typeof source !== 'string') return;
      const base = source.startsWith('@') ? source.split('/').slice(0, 2).join('/') : source.split('/')[0];
      if (SIMULATION_PACKAGES.includes(base)) context.report({ node, messageId: 'forbidden', data: { name: source } });
    };
    return {
      ImportDeclaration: (node) => check(node, node.source.value),
      ImportExpression: (node) => node.source.type === 'Literal' && check(node, node.source.value),
      CallExpression: (node) => {
        if (node.callee.type === 'Identifier' && node.callee.name === 'require' && node.arguments[0]?.type === 'Literal') check(node, node.arguments[0].value);
      },
    };
  },
};

/** @type {import('eslint').Rule.RuleModule} */
const ioOnlyInProtocol = {
  meta: {
    type: 'problem',
    docs: { description: 'only packages/protocol may use WebSocket, fetch and other network APIs' },
    schema: [],
    messages: { forbidden: "'{{name}}' is only allowed in packages/protocol: every value shown comes through the session client." },
  },
  create(context) {
    const filename = context.filename ?? context.getFilename();
    if (/[\\/]packages[\\/]protocol[\\/]/.test(filename)) return {};
    return {
      Identifier(node) {
        if (!IO_GLOBALS.includes(node.name)) return;
        const parent = node.parent;
        // Only flag references, not property names or declarations.
        if (parent.type === 'MemberExpression' && parent.property === node && !parent.computed) return;
        if (parent.type === 'Property' && parent.key === node) return;
        if (parent.type === 'ImportSpecifier' || parent.type === 'TSPropertySignature') return;
        const scope = context.sourceCode.getScope(node);
        const ref = scope.references.find((r) => r.identifier === node);
        if (ref && ref.resolved && ref.resolved.defs.length > 0) return; // a local binding, not the global
        context.report({ node, messageId: 'forbidden', data: { name: node.name } });
      },
    };
  },
};

const plugin = {
  meta: { name: 'eslint-plugin-embsim', version: '0.1.0' },
  rules: { 'no-simulation-imports': noSimulationImports, 'io-only-in-protocol': ioOnlyInProtocol },
};

export default plugin;
