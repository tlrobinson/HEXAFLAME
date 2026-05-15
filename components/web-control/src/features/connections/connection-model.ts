import { RELAY_CHANNEL_COUNT } from "../../devices/relay";

export type ConnectionType = "relay" | "stepper";

export type Channel = {
  index: number;
  jetId: string | null;
  state?: string;
  motionState?: string;
  homed?: boolean;
  travelSteps?: number | null;
  positionPercent?: number;
};

export type Connection = {
  id: string;
  type: ConnectionType;
  name: string;
  expanded: boolean;
  portKey: string | null;
  port: unknown | null;
  reader: unknown | null;
  status: string;
  connectInProgress: boolean;
  syncInProgress: boolean;
  stateQueue: boolean[][];
  lastStates: Array<boolean | null>;
  queuedPosition: number | null;
  positionSendInProgress: boolean;
  positionSendTimerId: number | null;
  lastSendAtMs: number;
  channels: Channel[];
};

export type MappingTarget = {
  connectionId: string;
  channelIndex: number;
} | null;

let connectionSequence = 1;

export function makeChannel(index: number): Channel {
  return {
    index,
    jetId: null,
    state: "Unknown",
    motionState: "Unknown",
    homed: false,
    travelSteps: null,
    positionPercent: 50,
  };
}

export function makeChannels(type: ConnectionType): Channel[] {
  return [makeChannel(0)];
}

export function createConnection(
  type: ConnectionType,
  saved: Partial<Connection> = {},
): Connection {
  const id =
    saved.id ||
    `${type}-${Date.now().toString(36)}-${connectionSequence++}`;
  return {
    id,
    type,
    name:
      saved.name ||
      `${type === "relay" ? "Relay" : "Stepper"} ${connectionSequence++}`,
    expanded: saved.expanded !== false,
    portKey: saved.portKey || null,
    port: null,
    reader: null,
    status:
      saved.status ||
      `${type === "relay" ? "Relay sync" : "Stepper"} disconnected`,
    connectInProgress: false,
    syncInProgress: false,
    stateQueue: [],
    lastStates: Array(RELAY_CHANNEL_COUNT).fill(null),
    queuedPosition: null,
    positionSendInProgress: false,
    positionSendTimerId: null,
    lastSendAtMs: -Infinity,
    channels: Array.isArray(saved.channels) && saved.channels.length > 0
      ? saved.channels.map((savedChannel, index) => ({
          ...makeChannel(index),
          ...savedChannel,
          index,
        }))
      : makeChannels(type),
  };
}
