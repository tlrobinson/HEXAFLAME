import { splitLogPayload } from "./log-format";

export type DeviceLogRole = "midi" | "relay" | "stepper";
export type DeviceLogDirection = "event" | "rx" | "tx";
export type DeviceCommandRole = "relay" | "stepper";

export type DeviceLogEntry = {
  device: DeviceLogRole;
  direction: DeviceLogDirection;
  id: number;
  payload: string;
  timestampMs: number;
};

type LogFilters = {
  device: DeviceLogRole | "all";
  type: DeviceLogDirection | "all";
};

type LogPaneSnapshot = {
  activeTab: "editor" | "logs";
  commandDevice: DeviceCommandRole;
  commandEnabled: Record<DeviceCommandRole, boolean>;
  collapsed: boolean;
  filters: LogFilters;
  logs: DeviceLogEntry[];
  width: number;
};

type DeviceCommandHandler = (command: string) => Promise<boolean> | boolean;

const STORAGE_KEY = "hexagon-rings-state";
const LOG_PANE_COLLAPSED_KEY = `${STORAGE_KEY}:log-pane-collapsed`;
const RIGHT_PANE_WIDTH_KEY = `${STORAGE_KEY}:right-pane-width`;
const DEVICE_LOG_LIMIT = 500;

const listeners = new Set<() => void>();
const commandHandlers: Partial<Record<DeviceCommandRole, DeviceCommandHandler>> = {};

let nextLogId = 1;
let snapshot: LogPaneSnapshot = {
  activeTab: "logs",
  commandDevice: "stepper",
  commandEnabled: {
    relay: false,
    stepper: false,
  },
  collapsed: getInitialCollapsed(),
  filters: {
    device: "all",
    type: "all",
  },
  logs: [],
  width: getInitialWidth(),
};

function getInitialCollapsed() {
  try {
    return window.localStorage.getItem(LOG_PANE_COLLAPSED_KEY) !== "0";
  } catch {
    return true;
  }
}

function getInitialWidth() {
  try {
    const saved = Number(window.localStorage.getItem(RIGHT_PANE_WIDTH_KEY));
    if (Number.isFinite(saved)) {
      return Math.min(Math.max(saved, 320), 720);
    }
  } catch {
    // Ignore storage failures; the default width is usable.
  }
  return 420;
}

function emit() {
  for (const listener of listeners) {
    listener();
  }
}

export function subscribeToLogPane(listener: () => void) {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

export function getLogPaneSnapshot() {
  return snapshot;
}

export function appendDeviceLog(
  role: DeviceLogRole,
  direction: DeviceLogDirection,
  payload: string | BufferSource,
) {
  const timestampMs = Date.now();
  const entries = [...snapshot.logs];

  for (const formattedPayload of splitLogPayload(payload)) {
    entries.push({
      device: role,
      direction,
      id: nextLogId,
      payload: formattedPayload,
      timestampMs,
    });
    nextLogId += 1;
  }

  if (entries.length > DEVICE_LOG_LIMIT) {
    entries.splice(0, entries.length - DEVICE_LOG_LIMIT);
  }

  snapshot = {
    ...snapshot,
    logs: entries,
  };
  emit();
}

export function clearDeviceLogs() {
  snapshot = {
    ...snapshot,
    logs: [],
  };
  emit();
}

export function setLogPaneCollapsed(collapsed: boolean) {
  snapshot = {
    ...snapshot,
    collapsed,
  };
  try {
    window.localStorage.setItem(LOG_PANE_COLLAPSED_KEY, collapsed ? "1" : "0");
  } catch {
    // Ignore storage failures; the pane can still be toggled.
  }
  emit();
}

export function setLogPaneTab(activeTab: "editor" | "logs") {
  snapshot = {
    ...snapshot,
    activeTab,
  };
  emit();
}

export function setLogPaneWidth(width: number) {
  const nextWidth = Math.min(Math.max(width, 320), 720);
  snapshot = {
    ...snapshot,
    width: nextWidth,
  };
  try {
    window.localStorage.setItem(RIGHT_PANE_WIDTH_KEY, String(nextWidth));
  } catch {
    // Ignore storage failures; resizing should still work.
  }
  emit();
}

export function setLogFilters(filters: Partial<LogFilters>) {
  snapshot = {
    ...snapshot,
    filters: {
      ...snapshot.filters,
      ...filters,
    },
  };
  emit();
}

export function setCommandDevice(device: DeviceCommandRole) {
  snapshot = {
    ...snapshot,
    commandDevice: device,
  };
  emit();
}

export function setDeviceCommandEnabled(
  role: DeviceCommandRole,
  enabled: boolean,
) {
  if (snapshot.commandEnabled[role] === enabled) {
    return;
  }
  snapshot = {
    ...snapshot,
    commandEnabled: {
      ...snapshot.commandEnabled,
      [role]: enabled,
    },
  };
  emit();
}

export function setDeviceCommandHandler(
  role: DeviceCommandRole,
  handler: DeviceCommandHandler,
) {
  commandHandlers[role] = handler;
}

export async function sendDeviceCommand(role: DeviceCommandRole, command: string) {
  const handler = commandHandlers[role];
  if (!handler) {
    return false;
  }
  return Boolean(await handler(command));
}
