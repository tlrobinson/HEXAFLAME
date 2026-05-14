import { useSyncExternalStore } from "react";

export const sequenceOptions = [
  { label: "Ripple", value: "ripple" },
  { label: "Ripple Without Spokes", value: "ripple-outline" },
  { label: "Concentric Band", value: "band" },
  { label: "Band Without Spokes", value: "band-outline" },
  { label: "Perimeter Chase", value: "chase" },
  { label: "Chase Without Spokes", value: "chase-outline" },
  { label: "Graph Walker", value: "walker" },
  { label: "Walker Without Spokes", value: "walker-outline" },
];

type AnimationControlsSnapshot = {
  loopEnabled: boolean;
  playing: boolean;
  selectedSequenceId: string;
  speed: number;
};

type AnimationControlsCallbacks = {
  onLoopToggle: () => void;
  onPlayPause: () => void;
  onSequenceChange: (sequenceId: string) => void;
  onSpeedChange: (speed: number) => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: AnimationControlsSnapshot = {
  loopEnabled: true,
  playing: false,
  selectedSequenceId: "ripple",
  speed: 12,
};

let callbacks: AnimationControlsCallbacks = {
  onLoopToggle: noop,
  onPlayPause: noop,
  onSequenceChange: noop,
  onSpeedChange: noop,
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

export function setAnimationControlsSnapshot(
  nextSnapshot: AnimationControlsSnapshot,
) {
  snapshot = nextSnapshot;
  emit();
}

export function setAnimationControlsCallbacks(
  nextCallbacks: AnimationControlsCallbacks,
) {
  callbacks = nextCallbacks;
  emit();
}

export function useAnimationControls() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
