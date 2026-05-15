import { useSyncExternalStore } from "react";

export type MidiNoteMapping = {
  id: string;
  note: number | null;
  jetId: string | null;
};

type MidiSnapshot = {
  connected: boolean;
  deviceName: string;
  enabled: boolean;
  expanded: boolean;
  heldNotes: Set<number>;
  learningMappingId: string | null;
  mappingTargetId: string | null;
  mappings: MidiNoteMapping[];
  supported: boolean;
  status: string;
};

type MidiCallbacks = {
  onAddMapping: () => void;
  onConnect: () => void;
  onDelete: () => void;
  onDisconnect: () => void;
  onMap: (mapping: MidiNoteMapping) => void;
  onRemoveMapping: (mapping: MidiNoteMapping) => void;
  onRename: (name: string) => void;
  onToggleConfig: () => void;
};

const noop = () => {};
const listeners = new Set<() => void>();
const MIDI_ENABLED_KEY = "hexagon-rings-state:midi-device-enabled";
const MIDI_EXPANDED_KEY = "hexagon-rings-state:midi-device-expanded";
const MIDI_MAPPINGS_KEY = "hexagon-rings-state:midi-note-mappings";

let snapshot: MidiSnapshot = {
  connected: false,
  deviceName: getInitialDeviceName(),
  enabled: getInitialMidiEnabled(),
  expanded: getInitialMidiExpanded(),
  heldNotes: new Set(),
  learningMappingId: null,
  mappingTargetId: null,
  mappings: getInitialMappings(),
  supported: true,
  status: "Disconnected",
};

let callbacks: MidiCallbacks = {
  onAddMapping: noop,
  onConnect: noop,
  onDelete: noop,
  onDisconnect: noop,
  onMap: noop,
  onRemoveMapping: noop,
  onRename: noop,
  onToggleConfig: noop,
};

function getInitialDeviceName() {
  try {
    return window.localStorage.getItem("hexagon-rings-state:midi-device-name") || "MIDI";
  } catch {
    return "MIDI";
  }
}

function getInitialMidiEnabled() {
  try {
    return window.localStorage.getItem(MIDI_ENABLED_KEY) !== "false";
  } catch {
    return true;
  }
}

function getInitialMidiExpanded() {
  try {
    return window.localStorage.getItem(MIDI_EXPANDED_KEY) === "true";
  } catch {
    return false;
  }
}

function getInitialMappings() {
  try {
    const parsed = JSON.parse(window.localStorage.getItem(MIDI_MAPPINGS_KEY) || "null");
    if (Array.isArray(parsed)) {
      return parsed
        .map((mapping, index): MidiNoteMapping | null => {
          const note =
            mapping?.note === null ? null : Number(mapping?.note);
          return {
            id: typeof mapping.id === "string" ? mapping.id : `midi-note-${index}`,
            note: Number.isFinite(note) ? note : null,
            jetId: typeof mapping.jetId === "string" ? mapping.jetId : null,
          };
        })
        .filter((mapping): mapping is MidiNoteMapping => Boolean(mapping));
    }
  } catch {
    // Fall through to defaults.
  }

  return [];
}

function persistMidiEnabled(enabled: boolean) {
  try {
    window.localStorage.setItem(MIDI_ENABLED_KEY, String(enabled));
  } catch {
    // Ignore storage failures.
  }
}

function persistMidiExpanded(expanded: boolean) {
  try {
    window.localStorage.setItem(MIDI_EXPANDED_KEY, String(expanded));
  } catch {
    // Ignore storage failures.
  }
}

export function persistMidiMappings(mappings: MidiNoteMapping[]) {
  try {
    window.localStorage.setItem(MIDI_MAPPINGS_KEY, JSON.stringify(mappings));
  } catch {
    // Ignore storage failures.
  }
}

function emit() {
  for (const listener of listeners) {
    listener();
  }
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

function getSnapshot() {
  return snapshot;
}

export function getMidiSnapshot() {
  return snapshot;
}

export function setMidiCallbacks(nextCallbacks: MidiCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function setMidiSnapshot(nextSnapshot: Partial<MidiSnapshot>) {
  snapshot = {
    ...snapshot,
    ...nextSnapshot,
  };
  if (Object.prototype.hasOwnProperty.call(nextSnapshot, "enabled")) {
    persistMidiEnabled(snapshot.enabled);
  }
  if (Object.prototype.hasOwnProperty.call(nextSnapshot, "expanded")) {
    persistMidiExpanded(snapshot.expanded);
  }
  if (Object.prototype.hasOwnProperty.call(nextSnapshot, "mappings")) {
    persistMidiMappings(snapshot.mappings);
  }
  emit();
}

export function setMidiStatus(status: string) {
  setMidiSnapshot({ status });
}

export function setMidiDeviceName(deviceName: string) {
  snapshot = {
    ...snapshot,
    deviceName,
  };
  try {
    window.localStorage.setItem("hexagon-rings-state:midi-device-name", deviceName);
  } catch {
    // Ignore storage failures; the name is still updated in memory.
  }
  emit();
}

export function useMidiConnection() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
