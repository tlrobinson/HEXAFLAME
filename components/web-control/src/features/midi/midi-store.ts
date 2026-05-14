import { useSyncExternalStore } from "react";

type MidiSnapshot = {
  connected: boolean;
  supported: boolean;
  status: string;
};

type MidiCallbacks = {
  onConnect: () => void;
  onDisconnect: () => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: MidiSnapshot = {
  connected: false,
  supported: true,
  status: "Disconnected",
};

let callbacks: MidiCallbacks = {
  onConnect: noop,
  onDisconnect: noop,
};

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

export function setMidiCallbacks(nextCallbacks: MidiCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function setMidiSnapshot(nextSnapshot: Partial<MidiSnapshot>) {
  snapshot = {
    ...snapshot,
    ...nextSnapshot,
  };
  emit();
}

export function setMidiStatus(status: string) {
  setMidiSnapshot({ status });
}

export function useMidiConnection() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
