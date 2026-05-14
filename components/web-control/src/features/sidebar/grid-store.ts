import { useSyncExternalStore } from "react";

type GridSnapshot = {
  jetMode: string;
  labelMode: string;
  rings: number;
};

type GridCallbacks = {
  onAllOff: () => void;
  onAllOn: () => void;
  onJetModeChange: (jetMode: string) => void;
  onLabelModeChange: (labelMode: string) => void;
  onReset: () => void;
  onRingsChange: (rings: number) => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: GridSnapshot = {
  jetMode: "all",
  labelMode: "address",
  rings: 4,
};

let callbacks: GridCallbacks = {
  onAllOff: noop,
  onAllOn: noop,
  onJetModeChange: noop,
  onLabelModeChange: noop,
  onReset: noop,
  onRingsChange: noop,
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

export function setGridSnapshot(nextSnapshot: GridSnapshot) {
  snapshot = nextSnapshot;
  emit();
}

export function setGridCallbacks(nextCallbacks: GridCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function useGridControls() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
