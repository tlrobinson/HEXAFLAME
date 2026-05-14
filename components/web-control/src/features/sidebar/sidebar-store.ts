import { useSyncExternalStore } from "react";

type SidebarSnapshot = {
  collapsed: boolean;
  width: number;
};

type SidebarCallbacks = {
  onToggle: () => void;
  onTransitionEnd: () => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: SidebarSnapshot = {
  collapsed: false,
  width: getInitialWidth(),
};

function getInitialWidth() {
  try {
    const saved = Number(window.localStorage.getItem("hexagon-rings-state:left-pane-width"));
    if (Number.isFinite(saved)) {
      return Math.min(Math.max(saved, 240), 520);
    }
  } catch {
    // Ignore storage failures; the default width is usable.
  }
  return 300;
}

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
    ...snapshot,
    collapsed,
  };
  emit();
}

export function setSidebarWidth(width: number) {
  const nextWidth = Math.min(Math.max(width, 240), 520);
  snapshot = {
    ...snapshot,
    width: nextWidth,
  };
  try {
    window.localStorage.setItem(
      "hexagon-rings-state:left-pane-width",
      String(nextWidth),
    );
  } catch {
    // Ignore storage failures; resizing should still work.
  }
  emit();
}

export function useSidebar() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
