"""Schema 1 projects. Unknown fields are errors, never ignored behavior."""
import json
import re
from copy import deepcopy

from .kernel import Kernel, timestamp


def fields(obj, required, optional=()):
    if not isinstance(obj, dict):
        raise ValueError("expected an object")
    missing, extra = set(required) - obj.keys(), obj.keys() - set(required) - set(optional)
    if missing or extra:
        raise ValueError(f"invalid fields: missing={sorted(missing)}, unknown={sorted(extra)}")


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{0,63}", value):
        raise ValueError("identifiers must contain 1–64 letters, digits, underscores or hyphens and start with a letter")
    return value


def validate(data):
    fields(data, ("schema_version", "name", "nets", "components"), ("events",))
    if type(data["schema_version"]) is not int or data["schema_version"] != 1:
        raise ValueError("unsupported schema_version; expected 1")
    identifier(data["name"])
    nets = data["nets"]
    if not isinstance(nets, list) or not 1 <= len(nets) <= 256:
        raise ValueError("nets must contain 1–256 identifiers")
    for net in nets:
        identifier(net)
    if len(set(nets)) != len(nets):
        raise ValueError("duplicate net")
    components = data["components"]
    if not isinstance(components, list) or not 1 <= len(components) <= 1024:
        raise ValueError("components must contain 1–1024 objects")
    ids = {}
    for component in components:
        if not isinstance(component, dict):
            raise ValueError("component must be an object")
        kind = component.get("kind")
        optional = {"button": ("active_level",), "pull": ("level",), "indicator": ()}
        if not isinstance(kind, str) or kind not in optional:
            raise ValueError(f"unsupported component kind: {kind!r}")
        fields(component, ("id", "kind", "net"), optional[kind])
        name = identifier(component["id"])
        if name in ids:
            raise ValueError(f"duplicate component: {name}")
        ids[name] = component
        if not isinstance(component["net"], str) or component["net"] not in nets:
            raise ValueError(f"unknown net for {name}")
        for key in optional[kind]:
            value = component.get(key, 1 if kind == "button" else 0)
            if type(value) is not int or value not in (0, 1):
                raise ValueError(f"{key} must be 0 or 1")
    events = data.get("events", [])
    if not isinstance(events, list) or len(events) > 7000:
        raise ValueError("events must be a list with at most 7000 entries")
    for event in events:
        fields(event, ("at_ns", "component", "pressed"))
        timestamp(event["at_ns"])
        target = event["component"]
        if not isinstance(target, str) or ids.get(target, {}).get("kind") != "button":
            raise ValueError("events must target an existing button")
        if type(event["pressed"]) is not bool:
            raise ValueError("pressed must be a boolean")
    return data


def load(path):
    with open(path, encoding="utf-8") as stream:
        return validate(json.load(stream))


class Project:
    def __init__(self, data):
        self.data = deepcopy(validate(data))
        data = self.data
        self.kernel = Kernel()
        self.inputs = []
        self.components = {c["id"]: c for c in data["components"]}
        self.drivers = {}
        try:
            self.nets = {name: self.kernel.add_net() for name in data["nets"]}
            for name, component in self.components.items():
                kind = component["kind"]
                if kind != "indicator":
                    driver = self.kernel.add_driver(self.nets[component["net"]], int(kind == "button"))
                    self.drivers[name] = driver
                    if kind == "pull":
                        self.kernel.schedule(0, driver, component.get("level", 0))
            self.kernel.advance(0)
            for event in data.get("events", []):
                self.set_button(event["component"], event["pressed"], event["at_ns"])
            self.kernel.advance(0)
        except Exception:
            self.close()
            raise

    def close(self):
        self.kernel.close()

    def set_button(self, name, pressed, at=None):
        if not isinstance(name, str) or self.components.get(name, {}).get("kind") != "button":
            raise ValueError("set requires an existing button component")
        if type(pressed) is not bool:
            raise ValueError("pressed must be a boolean")
        at = self.kernel.now if at is None else timestamp(at)
        level = self.components[name].get("active_level", 1) if pressed else 2
        self.kernel.schedule(at, self.drivers[name], level)
        self.inputs.append({"at_ns": at, "component": name, "pressed": pressed, "sequence": len(self.inputs)})

    def advance(self, until):
        self.kernel.advance(until)

    def snapshot(self):
        nets = {name: self.kernel.read(net) for name, net in self.nets.items()}
        components = {}
        for name, component in self.components.items():
            state = {"kind": component["kind"], "net": component["net"], "logic": nets[component["net"]]}
            if component["kind"] == "indicator":
                state["lit"] = state["logic"] == "1"
            if component["kind"] == "button":
                events = [e for e in self.inputs if e["component"] == name and e["at_ns"] <= self.kernel.now]
                state["pressed"] = max(events, key=lambda e: (e["at_ns"], e["sequence"]))["pressed"] if events else False
            components[name] = state
        return {"name": self.data["name"], "time_ns": self.kernel.now, "nets": nets,
                "components": components, "inputs": list(self.inputs),
                "fidelity": "ideal-digital; no firmware or electrical solver"}
