import { useSyncExternalStore } from "react";
import type { Channel, Connection, MappingTarget } from "./connection-model";

type ConnectionsSnapshot = {
  activeNodeIds: Set<string>;
  connections: Connection[];
  mappingTarget: MappingTarget;
  serialSupported: boolean;
};

type ConnectionCallbacks = {
  onAddRelay: () => void;
  onAddStepper: () => void;
  onConnect: (connection: Connection) => void;
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
  onAddRelay: noop,
  onAddStepper: noop,
  onConnect: noop,
  onDisconnect: noop,
  onHome: noop,
  onMap: noop,
  onPositionCommit: noop,
  onPositionInput: noop,
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
