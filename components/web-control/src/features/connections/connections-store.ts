import { useSyncExternalStore } from "react";
import type { Channel, Connection, ConnectionType, MappingTarget } from "./connection-model";

export type AddDeviceRequest =
  | {
      name: string;
      portKey: string | null;
      type: ConnectionType;
    }
  | {
      name: string;
      type: "midi";
    };

type ConnectionsSnapshot = {
  activeNodeIds: Set<string>;
  connections: Connection[];
  mappingTarget: MappingTarget;
  serialSupported: boolean;
};

type ConnectionCallbacks = {
  onAddChannel: (connection: Connection) => void;
  onAddDevice: (device: AddDeviceRequest) => void;
  onConnect: (connection: Connection) => void;
  onDelete: (connection: Connection) => void;
  onDisconnect: (connection: Connection) => void;
  onHome: (connection: Connection, channel: Channel) => void;
  onMap: (connection: Connection, channel: Channel) => void;
  onPositionCommit: (
    connection: Connection,
    channel: Channel,
    positionPercent: number,
  ) => void;
  onPositionInput: (
    connection: Connection,
    channel: Channel,
    positionPercent: number,
  ) => void;
  onResetFault: (connection: Connection, channel: Channel) => void;
  onRemoveChannel: (connection: Connection) => void;
  onRename: (connection: Connection, name: string) => void;
  onToggleConfig: (connection: Connection) => void;
};

const noop = () => {};
const listeners = new Set<() => void>();

let snapshot: ConnectionsSnapshot = {
  activeNodeIds: new Set(),
  connections: [],
  mappingTarget: null,
  serialSupported: false,
};

let callbacks: ConnectionCallbacks = {
  onAddChannel: noop,
  onAddDevice: noop,
  onConnect: noop,
  onDelete: noop,
  onDisconnect: noop,
  onHome: noop,
  onMap: noop,
  onPositionCommit: noop,
  onPositionInput: noop,
  onResetFault: noop,
  onRemoveChannel: noop,
  onRename: noop,
  onToggleConfig: noop,
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

export function setConnectionsSnapshot(nextSnapshot: ConnectionsSnapshot) {
  snapshot = nextSnapshot;
  emit();
}

export function setConnectionCallbacks(nextCallbacks: ConnectionCallbacks) {
  callbacks = nextCallbacks;
  emit();
}

export function useConnectionsPanel() {
  return {
    callbacks,
    snapshot: useSyncExternalStore(subscribe, getSnapshot, getSnapshot),
  };
}
