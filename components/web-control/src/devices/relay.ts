import type { Scene, SceneNode } from "../types";

export const RELAY_CHANNEL_COUNT = 32;
const RELAY_SLAVE_ADDRESS = 1;

export type SerialPortRole = "relay" | "stepper";

export function crc16Modbus(bytes: Iterable<number>) {
  let crc = 0xffff;

  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xa001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

export function buildRelayWriteFrame(channelIndex: number, enabled: boolean) {
  const payload = [
    RELAY_SLAVE_ADDRESS,
    0x05,
    (channelIndex >> 8) & 0xff,
    channelIndex & 0xff,
    enabled ? 0xff : 0x00,
    0x00,
  ];
  const crc = crc16Modbus(payload);
  return new Uint8Array([...payload, crc & 0xff, (crc >> 8) & 0xff]);
}

export function buildRelayWriteMultipleFrame(states: boolean[]) {
  const coilBytes = Array(4).fill(0);

  for (let index = 0; index < RELAY_CHANNEL_COUNT; index += 1) {
    if (!states[index]) {
      continue;
    }

    coilBytes[Math.floor(index / 8)] |= 1 << (index % 8);
  }

  const payload = [
    RELAY_SLAVE_ADDRESS,
    0x0f,
    0x00,
    0x00,
    0x00,
    RELAY_CHANNEL_COUNT,
    coilBytes.length,
    ...coilBytes,
  ];
  const crc = crc16Modbus(payload);
  return new Uint8Array([...payload, crc & 0xff, (crc >> 8) & 0xff]);
}

export function getRelayMappedNodeIds(
  currentScene: Scene,
  distanceMap: Map<string, number>,
  angleFromCenter: (node: SceneNode) => number,
) {
  return [...currentScene.nodes]
    .sort((left, right) => {
      const distanceDelta =
        (distanceMap.get(left.id) ?? 0) - (distanceMap.get(right.id) ?? 0);
      if (distanceDelta !== 0) {
        return distanceDelta;
      }

      const typeDelta =
        (left.type === "center" ? 0 : 1) - (right.type === "center" ? 0 : 1);
      if (typeDelta !== 0) {
        return typeDelta;
      }

      return angleFromCenter(left) - angleFromCenter(right);
    })
    .slice(0, RELAY_CHANNEL_COUNT)
    .map((node) => node.id);
}

export function buildRelayStates(
  currentScene: Scene,
  mappedNodeIds: string[],
  activeNodeIds: Set<string>,
) {
  const states = Array(RELAY_CHANNEL_COUNT).fill(false);

  for (let index = 0; index < mappedNodeIds.length; index += 1) {
    states[index] = activeNodeIds.has(mappedNodeIds[index]);
  }

  return {
    mappedNodeIds,
    states,
  };
}

export function relayStatesEqual(left: unknown[], right: unknown[]) {
  if (left.length !== right.length) {
    return false;
  }

  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) {
      return false;
    }
  }

  return true;
}

export function parseRelayCommandInput(command: string) {
  const trimmed = command.trim();
  if (!trimmed) {
    return null;
  }

  const hexPrefixMatch = trimmed.match(/^hex\s*:\s*(.+)$/i);
  const hexText = hexPrefixMatch ? hexPrefixMatch[1].trim() : trimmed;
  const hexTokens = hexText.split(/[\s,]+/).filter(Boolean);
  const looksLikeHexBytes =
    hexTokens.length > 0 &&
    hexTokens.every((token) => /^(?:0x)?[0-9a-f]{1,2}$/i.test(token));

  if (hexPrefixMatch || looksLikeHexBytes) {
    if (!looksLikeHexBytes) {
      throw new Error("Relay hex bytes must be 00-ff, separated by spaces or commas");
    }
    return new Uint8Array(
      hexTokens.map((token) => Number.parseInt(token.replace(/^0x/i, ""), 16)),
    );
  }

  return new TextEncoder().encode(`${command}\n`);
}

export function getSerialPortKey(port: unknown) {
  const info = (port as { getInfo?: () => Record<string, unknown> })?.getInfo?.();
  if (!info) {
    return null;
  }

  if (
    Number.isFinite(info.usbVendorId) ||
    Number.isFinite(info.usbProductId)
  ) {
    return `usb:${info.usbVendorId ?? 0}:${info.usbProductId ?? 0}`;
  }

  if (Number.isFinite(info.bluetoothServiceClassId)) {
    return `bt:${info.bluetoothServiceClassId}`;
  }

  return null;
}

export function getRolePortStorageKey(
  role: SerialPortRole,
  relayStorageKey: string,
  stepperStorageKey: string,
) {
  return role === "relay" ? relayStorageKey : stepperStorageKey;
}

export function getPreferredSerialPortKey(
  role: SerialPortRole,
  relayStorageKey: string,
  stepperStorageKey: string,
) {
  try {
    return window.localStorage.getItem(
      getRolePortStorageKey(role, relayStorageKey, stepperStorageKey),
    );
  } catch {
    return null;
  }
}

export function setPreferredSerialPortKey(
  role: SerialPortRole,
  portKey: string | null,
  relayStorageKey: string,
  stepperStorageKey: string,
) {
  try {
    const storageKey = getRolePortStorageKey(role, relayStorageKey, stepperStorageKey);
    if (!portKey) {
      window.localStorage.removeItem(storageKey);
      return;
    }
    window.localStorage.setItem(storageKey, portKey);
  } catch {
    // Ignore storage failures so serial controls still work.
  }
}

export function rememberSerialPortRole(
  role: SerialPortRole,
  port: unknown,
  relayStorageKey: string,
  stepperStorageKey: string,
) {
  const portKey = getSerialPortKey(port);
  if (!portKey) {
    return;
  }

  setPreferredSerialPortKey(role, portKey, relayStorageKey, stepperStorageKey);
  const otherRole = role === "relay" ? "stepper" : "relay";
  if (
    getPreferredSerialPortKey(otherRole, relayStorageKey, stepperStorageKey) ===
    portKey
  ) {
    setPreferredSerialPortKey(otherRole, null, relayStorageKey, stepperStorageKey);
  }
}

export function findPreferredSerialPort<TPort>(
  ports: TPort[],
  role: SerialPortRole,
  relayStorageKey: string,
  stepperStorageKey: string,
) {
  const preferredKey = getPreferredSerialPortKey(
    role,
    relayStorageKey,
    stepperStorageKey,
  );
  if (!preferredKey) {
    return null;
  }

  return ports.find((port) => getSerialPortKey(port) === preferredKey) || null;
}
