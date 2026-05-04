export const MIDI_NOTE_STEPPER_1_ENV = 36;
export const MIDI_NOTE_ALL_OFF = 48;
export const MIDI_NOTE_ALL_ON = 51;

export const MIDI_CC_STEPPER_1_ATTACK = 30;
export const MIDI_CC_STEPPER_1_DECAY = 31;
export const MIDI_CC_STEPPER_1_SUSTAIN = 32;
export const MIDI_CC_STEPPER_1_RELEASE = 33;
export const MIDI_CC_SPEED = 36;
export const MIDI_CC_STEPPER_1 = 37;
export const MIDI_CC_BACK = 25;
export const MIDI_CC_NEXT = 26;
export const MIDI_CC_PLAY = 27;
export const MIDI_CC_PAUSE = 28;

// M-VAVE SMC-PAD: pad note -> relay channel index.
export const MIDI_PAD_MAP = new Map<number, number>([
  [49, 0],
  [50, 1],
  [47, 2],
  [42, 3],
  [41, 4],
  [44, 5],
]);

export interface MidiInputLike {
  id: string;
  name?: string | null;
  manufacturer?: string | null;
  state?: string;
  onmidimessage: ((event: unknown) => void) | null;
}

interface MidiInputMapLike {
  values(): Iterable<MidiInputLike>;
  has(id: string): boolean;
}

export interface MidiAccessLike {
  inputs: MidiInputMapLike;
  onstatechange?: ((event?: unknown) => void) | null;
}

export function formatMidiBytes(bytes: Iterable<number>) {
  return [...bytes]
    .map((value) => value.toString(16).padStart(2, "0"))
    .join(" ");
}

export function getMidiInputLabel(input: Partial<MidiInputLike> | null | undefined) {
  return input?.name || input?.manufacturer || input?.id || "MIDI input";
}

export function formatMidiMessage(
  data: Iterable<number>,
  input: Partial<MidiInputLike> | null = null,
) {
  const bytes = [...data];
  const [status = 0, data1 = 0, data2 = 0] = bytes;
  const channel = (status & 0x0f) + 1;
  const type = status & 0xf0;
  const source = input ? `${getMidiInputLabel(input)}: ` : "";
  const suffix = ` (${formatMidiBytes(bytes)})`;

  if (type === 0x80 || (type === 0x90 && data2 === 0)) {
    return `${source}Note Off ch${channel} note ${data1}${suffix}`;
  }

  if (type === 0x90) {
    return `${source}Note On ch${channel} note ${data1} vel ${data2}${suffix}`;
  }

  if (type === 0xb0) {
    return `${source}CC ch${channel} #${data1} = ${data2}${suffix}`;
  }

  if (type === 0xc0) {
    return `${source}Program Change ch${channel} ${data1}${suffix}`;
  }

  if (type === 0xe0) {
    const value = ((data2 & 0x7f) << 7) | (data1 & 0x7f);
    return `${source}Pitch Bend ch${channel} ${value}${suffix}`;
  }

  return `${source}MIDI ${suffix}`;
}

export function connectMidiInput(
  input: MidiInputLike,
  connectedInputs: Map<string, MidiInputLike>,
  handleMessage: (event: unknown) => void,
) {
  const label = getMidiInputLabel(input);

  if (connectedInputs.has(input.id)) {
    input.onmidimessage = handleMessage;
    return {
      connected: false,
      label,
      totalInputs: connectedInputs.size,
    };
  }

  input.onmidimessage = handleMessage;
  connectedInputs.set(input.id, input);
  return {
    connected: true,
    label,
    totalInputs: connectedInputs.size,
  };
}

export function connectAllMidiInputs(
  midiAccess: MidiAccessLike,
  connectedInputs: Map<string, MidiInputLike>,
  handleMessage: (event: unknown) => void,
) {
  const availableInputs = [...midiAccess.inputs.values()].map(
    (input) => `${getMidiInputLabel(input)} [${input.state || "unknown"}]`,
  );
  const connectedLabels: string[] = [];
  let connectableCount = 0;

  for (const input of midiAccess.inputs.values()) {
    if (input.state !== "connected" && input.state !== undefined) {
      continue;
    }

    const result = connectMidiInput(input, connectedInputs, handleMessage);
    connectableCount += 1;
    if (result.connected) {
      connectedLabels.push(result.label);
    }
  }

  const disconnectedLabels: string[] = [];
  for (const [id, input] of connectedInputs) {
    if (input.state === "connected" && midiAccess.inputs.has(id)) {
      continue;
    }

    input.onmidimessage = null;
    connectedInputs.delete(id);
    disconnectedLabels.push(getMidiInputLabel(input));
  }

  return {
    availableInputs,
    connectedLabels,
    connectableCount,
    totalInputs: connectedInputs.size,
    disconnectedLabels,
  };
}

export function disconnectMidiInputs(connectedInputs: Map<string, MidiInputLike>) {
  const hadInputs = connectedInputs.size > 0;

  for (const input of connectedInputs.values()) {
    input.onmidimessage = null;
  }

  connectedInputs.clear();
  return hadInputs;
}
