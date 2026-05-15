import { useSyncExternalStore } from "react";

type EnvelopeSnapshot = {
  attackLabel: string;
  attackX: string;
  decayLabel: string;
  decayX: string;
  fillPath: string;
  markerX: string;
  markerY: string;
  noteLabel: string;
  path: string;
  releaseLabel: string;
  releaseX: string;
  sustainY: string;
  sustainLabel: string;
  sweepX: string;
  sweepY1: string;
  sweepY2: string;
};

type EnvelopeCallbacks = {
  onAttackChange: (x: number) => void;
  onDecayChange: (x: number) => void;
  onReleaseChange: (x: number) => void;
  onSustainChange: (y: number) => void;
};

const listeners = new Set<() => void>();
const noop = () => {};

let snapshot: EnvelopeSnapshot = {
  attackLabel: "A 180ms",
  attackX: "70",
  decayLabel: "D 220ms",
  decayX: "124",
  fillPath: "M14 78 L14 78 L14 78 Z",
  markerX: "14",
  markerY: "78",
  noteLabel: "No note",
  path: "M14 78 L70 14 L124 40 L190 40 L246 78",
  releaseLabel: "R 320ms",
  releaseX: "190",
  sustainY: "40",
  sustainLabel: "S 55%",
  sweepX: "14",
  sweepY1: "78",
  sweepY2: "78",
};

let callbacks: EnvelopeCallbacks = {
  onAttackChange: noop,
  onDecayChange: noop,
  onReleaseChange: noop,
  onSustainChange: noop,
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

export function setEnvelopeCallbacks(nextCallbacks: EnvelopeCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function useEnvelope() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
