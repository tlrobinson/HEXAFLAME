import { useSyncExternalStore } from "react";

type EnvelopeSnapshot = {
  attackLabel: string;
  decayLabel: string;
  fillPath: string;
  markerX: string;
  markerY: string;
  path: string;
  releaseLabel: string;
  sustainLabel: string;
  sweepX: string;
  sweepY1: string;
  sweepY2: string;
};

const listeners = new Set<() => void>();

let snapshot: EnvelopeSnapshot = {
  attackLabel: "A 180ms",
  decayLabel: "D 220ms",
  fillPath: "M14 78 L14 78 L14 78 Z",
  markerX: "14",
  markerY: "78",
  path: "M14 78 L70 14 L124 40 L190 40 L246 78",
  releaseLabel: "R 320ms",
  sustainLabel: "S 55%",
  sweepX: "14",
  sweepY1: "78",
  sweepY2: "78",
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

export function setEnvelopeSnapshot(nextSnapshot: EnvelopeSnapshot) {
  snapshot = nextSnapshot;
  emit();
}

export function useEnvelope() {
  return useSyncExternalStore(subscribe, getSnapshot, getSnapshot);
}
