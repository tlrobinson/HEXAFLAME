import { splitLogPayload } from "./log-format";

export type DeviceLogRole = "midi" | "relay" | "stepper";
export type DeviceLogDirection = "event" | "rx" | "tx";

export type DeviceLogEntry = {
  direction: DeviceLogDirection;
  payload: string;
  timestampMs: number;
};

type DeviceLogs = Record<DeviceLogRole, DeviceLogEntry[]>;

type LogPaneSnapshot = {
  commandEnabled: Record<"relay" | "stepper", boolean>;
  collapsed: boolean;
  logs: DeviceLogs;
};

type DeviceCommandRole = "relay" | "stepper";
type DeviceCommandHandler = (command: string) => Promise<boolean> | boolean;

const STORAGE_KEY = "hexagon-rings-state";
const LOG_PANE_COLLAPSED_KEY = `${STORAGE_KEY}:log-pane-collapsed`;
const DEVICE_LOG_LIMIT = 250;
const emptyLogs = (): DeviceLogs => ({
  midi: [],
  relay: [],
  stepper: [],
});

const listeners = new Set<() => void>();
const commandHandlers: Partial<Record<DeviceCommandRole, DeviceCommandHandler>> = {};

let snapshot: LogPaneSnapshot = {
  commandEnabled: {
    relay: false,
    stepper: false,
  },
  collapsed: getInitialCollapsed(),
  logs: emptyLogs(),
};

function getInitialCollapsed() {
  try {
    return window.localStorage.getItem(LOG_PANE_COLLAPSED_KEY) !== "0";
  } catch {
    return true;
  }
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
  const entries = [...snapshot.logs[role]];

  for (const formattedPayload of splitLogPayload(payload)) {
    entries.push({
      direction,
      payload: formattedPayload,
      timestampMs,
    });
  }

  if (entries.length > DEVICE_LOG_LIMIT) {
    entries.splice(0, entries.length - DEVICE_LOG_LIMIT);
  }

  snapshot = {
    ...snapshot,
    logs: {
      ...snapshot.logs,
      [role]: entries,
    },
  };
  emit();
}

export function clearDeviceLogs() {
  snapshot = {
    ...snapshot,
    logs: emptyLogs(),
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
