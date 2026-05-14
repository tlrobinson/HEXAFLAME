import { useSyncExternalStore } from "react";

type JetStats = {
  total: number;
  visible: number;
};

type StatsSnapshot = {
  outline: JetStats;
  spoke: JetStats;
};

const listeners = new Set<() => void>();
let snapshot: StatsSnapshot = {
  outline: { total: 0, visible: 0 },
  spoke: { total: 0, visible: 0 },
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

export function setStats(nextSnapshot: StatsSnapshot) {
  snapshot = nextSnapshot;
  emit();
}

export function useStats() {
  return useSyncExternalStore(subscribe, getSnapshot, getSnapshot);
}
