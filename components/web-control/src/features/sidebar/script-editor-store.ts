import { useSyncExternalStore } from "react";

type ScriptEditorSnapshot = {
  error: string | null;
  helpOpen: boolean;
  open: boolean;
  script: string;
};

type ScriptEditorCallbacks = {
  onChange: (script: string) => void;
  onRestore: () => void;
  onToggleHelp: () => void;
  onToggleOpen: () => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: ScriptEditorSnapshot = {
  error: null,
  helpOpen: false,
  open: false,
  script: "",
};

let callbacks: ScriptEditorCallbacks = {
  onChange: noop,
  onRestore: noop,
  onToggleHelp: noop,
  onToggleOpen: noop,
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

export function setScriptEditorSnapshot(
  nextSnapshot: Partial<ScriptEditorSnapshot>,
) {
  snapshot = {
    ...snapshot,
    ...nextSnapshot,
  };
  emit();
}

export function setScriptEditorCallbacks(
  nextCallbacks: ScriptEditorCallbacks,
) {
  callbacks = nextCallbacks;
  emit();
}

export function useScriptEditor() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
