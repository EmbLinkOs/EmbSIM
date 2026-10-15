"use strict";
let state;
let busy = false;
const byId = id => document.getElementById(id);

async function request(action) {
  const response = await fetch(action ? "/api/control" : "/api/state", action ? {
    method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(action)
  } : {});
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || "Request failed");
  return result;
}

function render(next) {
  state = next;
  byId("name").textContent = state.name;
  byId("clock").textContent = `${state.running ? "● Running" : "○ Paused"} · ${state.time_ns.toLocaleString()} ns`;
  drawCircuit();
  const container = byId("components");
  // Keep control nodes stable during polling so pointer/focus interactions survive.
  for (const [name, component] of Object.entries(state.components)) {
    let row = document.getElementById(`component-${name}`);
    if (!row) {
      row = document.createElement("div"); row.className = "component"; row.id = `component-${name}`;
      const label = document.createElement("strong"); label.textContent = name; row.append(label);
      const value = document.createElement("span"); value.className = "value"; row.append(value);
      if (component.kind === "button") {
        const button = document.createElement("button");
        button.addEventListener("click", () => control({action: "set", component: name, pressed: !state.components[name].pressed}));
        row.append(button);
      }
      container.append(row);
    }
    row.querySelector(".value").textContent = `${component.kind} · ${component.logic}`;
    row.classList.toggle("lit", component.kind === "indicator" && component.lit);
    const button = row.querySelector("button");
    if (button) {
      button.textContent = component.pressed ? "Release" : "Press";
      button.setAttribute("aria-pressed", String(component.pressed));
    }
  }
  byId("nets").textContent = JSON.stringify(state.nets, null, 2);
  byId("events").textContent = JSON.stringify(state.inputs.slice(-30), null, 2);
}

async function control(command) {
  commands = commands.then(async () => {
    busy = true;
    try { render(await request(command)); byId("error").textContent = ""; }
    catch (error) { byId("error").textContent = error.message; }
    finally { busy = false; }
  });
}
let commands = Promise.resolve();
for (const action of ["run", "pause", "reset"]) byId(action).onclick = () => control({action});
byId("step").onclick = () => { if (state) control({action: "advance", until_ns: state.time_ns + 1000000}); };
byId("export").onclick = () => {
  if (!state) return;
  const url = URL.createObjectURL(new Blob([JSON.stringify(state, null, 2)], {type: "application/json"}));
  const link = document.createElement("a"); link.href = url; link.download = `${state.name}-snapshot.json`; link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
};

function drawCircuit() {
  const svg = byId("circuit");
  svg.replaceChildren();
  const nets = Object.entries(state.nets);
  const maxComponents = Math.max(3, ...nets.map(([net]) => Object.values(state.components).filter(c => c.net === net).length));
  const width = Math.max(900, maxComponents * 200);
  svg.setAttribute("viewBox", `0 0 ${width} ${Math.max(240, nets.length * 180 + 40)}`);
  function element(tag, attributes, text) {
    const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
    if (text !== undefined) node.textContent = text;
    svg.append(node); return node;
  }
  for (const [index, [net, logic]] of nets.entries()) {
    const y = 30 + index * 180;
    const color = logic === "1" ? "#79e8bf" : logic === "X" ? "#ffb4a8" : logic === "Z" ? "#d6bc83" : "#6d8cb3";
    const components = Object.entries(state.components).filter(([, c]) => c.net === net);
    element("line", {x1: 70, y1: y + 120, x2: width - 70, y2: y + 120, stroke: color, "stroke-width": 2});
    element("text", {x: 70, y: y + 146, fill: color, "font-size": 11, "font-family": "monospace"}, `${net} · ${logic}`);
    components.forEach(([name, component], i) => {
      const x = width * (i + 1) / (components.length + 1);
      element("line", {x1: x, y1: y + 82, x2: x, y2: y + 120, stroke: color, "stroke-width": 2});
      element("circle", {cx: x, cy: y + 120, r: 4, fill: color});
      element("rect", {x: x - 72, y, width: 144, height: 82, rx: 10, fill: component.lit ? "#1a3b33" : "#152333", stroke: component.lit ? "#79e8bf" : "#425873"});
      element("text", {x, y: y + 24, "text-anchor": "middle", fill: "#e6edf6", "font-size": 12, "font-family": "monospace"}, name);
      element("text", {x, y: y + 45, "text-anchor": "middle", fill: "#9cafc8", "font-size": 10}, component.kind);
      element("circle", {cx: x, cy: y + 64, r: 5, fill: component.lit || component.pressed ? "#79e8bf" : "#4b627d"});
    });
  }
}

async function poll() {
  if (!busy) {
    busy = true;
    try { render(await request()); byId("error").textContent = ""; }
    catch (error) { byId("error").textContent = error.message; }
    finally { busy = false; }
  }
  setTimeout(poll, 100);
}
poll();
