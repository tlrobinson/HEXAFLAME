import { useSyncExternalStore } from "react";

type SidebarSnapshot = {
  collapsed: boolean;
};

type SidebarCallbacks = {
  onToggle: () => void;
  onTransitionEnd: () => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: SidebarSnapshot = {
  collapsed: false,
};

let callbacks: SidebarCallbacks = {
  onToggle: noop,
  onTransitionEnd: noop,
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

export function setSidebarCallbacks(nextCallbacks: SidebarCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function setSidebarCollapsed(collapsed: boolean) {
  snapshot = {
    collapsed,
  };
  emit();
}

export function useSidebar() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
