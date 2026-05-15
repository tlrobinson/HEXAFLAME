export const STEPPER_SERIAL_BAUD = 115200;
export const STEPPER_SEND_DELAY_MS = 75;
export const STEPPER_ENVELOPE_SEND_DELAY_MS = 20;
export const STEPPER_HOME_RATE_HZ = 400;

export interface StepperProtocolUpdate {
  statusMessage?: string;
  homed?: boolean;
  motionState?: string;
  travelSteps?: number | null;
  positionPercent?: number;
}

export interface StepperJsonRpcRequest {
  jsonrpc: "2.0";
  id: string;
  method: string;
  params: Record<string, unknown>;
}

export interface WritableSerialPortLike {
  writable?: {
    getWriter(): {
      write(payload: Uint8Array): Promise<void>;
      releaseLock(): void;
    };
  } | null;
}

export interface ReadableSerialPortLike {
  readable?: {
    getReader(): {
      read(): Promise<{ value?: Uint8Array; done?: boolean }>;
      releaseLock(): void;
    };
  } | null;
}

let nextStepperRequestId = 1;

function clampPercent(percent: number) {
  return Math.min(Math.max(percent, 0), 100);
}

export function buildStepperJsonRpcRequest(
  method: string,
  params: Record<string, unknown> = {},
): StepperJsonRpcRequest {
  return {
    jsonrpc: "2.0",
    id: `web-control-${nextStepperRequestId++}`,
    method,
    params: { channel: 0, ...params },
  };
}

export function buildStepperPositionCommand(percent: number) {
  return buildStepperJsonRpcRequest("move-percent", {
    percent: clampPercent(percent),
  });
}

export function buildStepperTimedPositionCommand(
  percent: number,
  durationMs: number,
) {
  return buildStepperJsonRpcRequest("move-percent-time", {
    percent: clampPercent(percent),
    duration_ms: Math.max(1, Math.round(durationMs)),
  });
}

export function buildStepperHomeCommand(rateHz = STEPPER_HOME_RATE_HZ) {
  return buildStepperJsonRpcRequest("home", {
    hz: Math.max(1, Math.round(rateHz)),
  });
}

export function buildStepperAdsrCommand({
  attackPercent,
  attackMs,
  decayPercent,
  decayMs,
  sustainMs = 0,
  releasePercent,
  releaseMs,
}: {
  attackPercent: number;
  attackMs: number;
  decayPercent: number;
  decayMs: number;
  sustainMs?: number;
  releasePercent: number;
  releaseMs: number;
}) {
  return buildStepperJsonRpcRequest("adsr", {
    attack_percent: clampPercent(attackPercent),
    attack_ms: Math.max(1, Math.round(attackMs)),
    decay_percent: clampPercent(decayPercent),
    decay_ms: Math.max(1, Math.round(decayMs)),
    sustain_ms: Math.max(0, Math.round(sustainMs)),
    release_percent: clampPercent(releasePercent),
    release_ms: Math.max(1, Math.round(releaseMs)),
  });
}

export function buildStepperReleaseCommand(percent: number, durationMs: number) {
  return buildStepperJsonRpcRequest("release", {
    percent: clampPercent(percent),
    duration_ms: Math.max(1, Math.round(durationMs)),
  });
}

export function parseStepperProtocolLine(line: string): StepperProtocolUpdate {
  const json = parseJsonRpcLine(line);
  if (json) {
    return parseStepperJsonRpcMessage(json);
  }

  const calibratedMatch = line.match(/^Calibrated:\s*(yes|no)$/i);
  if (calibratedMatch) {
    const homed = calibratedMatch[1].toLowerCase() === "yes";
    return {
      statusMessage: line,
      homed,
      travelSteps: homed ? undefined : null,
    };
  }

  const travelMatch = line.match(/^Travel steps:\s*(\d+)$/i);
  if (travelMatch) {
    return {
      statusMessage: line,
      travelSteps: Number(travelMatch[1]),
    };
  }

  const movedMatch = line.match(/^Moved to\s+([0-9]+(?:\.[0-9]+)?)%$/i);
  if (movedMatch) {
    return {
      statusMessage: line,
      positionPercent: Number(movedMatch[1]),
    };
  }

  const currentPositionMatch = line.match(
    /^Current position:\s*([0-9]+(?:\.[0-9]+)?)%$/i,
  );
  if (currentPositionMatch) {
    return {
      statusMessage: line,
      positionPercent: Number(currentPositionMatch[1]),
    };
  }

  const motionStateMatch = line.match(/^Motion state:\s*(.+)$/i);
  if (motionStateMatch) {
    return {
      statusMessage: line,
      motionState: motionStateMatch[1],
    };
  }

  return { statusMessage: line };
}

function parseJsonRpcLine(line: string) {
  try {
    const parsed = JSON.parse(line);
    return parsed && typeof parsed === "object" ? parsed : null;
  } catch {
    return null;
  }
}

function finiteNumber(value: unknown) {
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function applyStepperResult(
  result: Record<string, unknown>,
  statusMessage: string,
): StepperProtocolUpdate {
  const update: StepperProtocolUpdate = { statusMessage };

  const homed =
    typeof result.homed === "boolean"
      ? result.homed
      : typeof result.calibrated === "boolean"
        ? result.calibrated
        : null;
  if (homed !== null) {
    update.homed = homed;
    if (!homed) {
      update.travelSteps = null;
    }
  }

  const travelSteps = finiteNumber(result.travel_steps);
  if (travelSteps !== null) {
    update.travelSteps = travelSteps;
  }

  const positionPercent =
    finiteNumber(result.position_percent) ??
    finiteNumber(result.current_position_percent);
  if (positionPercent !== null) {
    update.positionPercent = positionPercent;
  }

  if (typeof result.motion_state === "string") {
    update.motionState = result.motion_state;
  }

  return update;
}

function parseResponseEvent(data: Record<string, unknown>): StepperProtocolUpdate {
  const message = typeof data.message === "string" ? data.message : "Stepper response";
  const update: StepperProtocolUpdate = { statusMessage: message };
  if (typeof data.motion_state === "string") {
    update.motionState = data.motion_state;
  }
  const percentMatch = message.match(/\bpercent=([0-9]+(?:\.[0-9]+)?)/i);
  if (percentMatch) {
    update.positionPercent = Number(percentMatch[1]);
  }
  return update;
}

function parseStepperJsonRpcMessage(message: Record<string, unknown>): StepperProtocolUpdate {
  if (message.error && typeof message.error === "object") {
    const error = message.error as Record<string, unknown>;
    return {
      statusMessage:
        typeof error.message === "string" ? error.message : "Stepper RPC error",
    };
  }

  if (message.result && typeof message.result === "object") {
    return applyStepperResult(
      message.result as Record<string, unknown>,
      typeof message.id === "string" ? `RPC ${message.id} complete` : "RPC complete",
    );
  }

  if (message.method === "log" && message.params && typeof message.params === "object") {
    const params = message.params as Record<string, unknown>;
    const level = typeof params.level === "string" ? params.level : "INFO";
    const text = typeof params.message === "string" ? params.message : "Stepper log";
    return { statusMessage: `${level}: ${text}` };
  }

  if (message.method === "event" && message.params && typeof message.params === "object") {
    const params = message.params as Record<string, unknown>;
    const event = typeof params.event === "string" ? params.event : "event";
    const data =
      params.data && typeof params.data === "object"
        ? (params.data as Record<string, unknown>)
        : {};
    const motionState = typeof data.motion_state === "string" ? data.motion_state : null;

    if (event === "ready") {
      return { statusMessage: "Stepper ready", motionState: motionState || undefined };
    }

    if (event === "response") {
      return parseResponseEvent(data);
    }

    if (event === "state-change") {
      return applyStepperResult(data, "Stepper state changed");
    }

    if (event === "move-started") {
      return {
        statusMessage: "Stepper move started",
        motionState: motionState || "moving",
      };
    }

    if (event === "envelope-phase") {
      const phase = typeof data.phase === "string" ? data.phase : "phase";
      return {
        statusMessage: `Stepper envelope ${phase}`,
        motionState: motionState || `envelope ${phase}`,
      };
    }

    if (event === "envelope-hold") {
      return {
        statusMessage: "Stepper envelope sustain",
        motionState: motionState || "envelope sustain",
      };
    }

    if (event === "command-received") {
      return {};
    }

    return {
      statusMessage: `Stepper ${event}`,
      motionState: motionState || undefined,
    };
  }

  return { statusMessage: lineSummary(message) };
}

function lineSummary(message: Record<string, unknown>) {
  if (typeof message.method === "string") {
    return `Stepper ${message.method}`;
  }
  return "Stepper message";
}

export function parseStepperCommandInput(input: string) {
  const trimmed = input.trim();
  if (!trimmed) {
    return null;
  }

  if (trimmed.startsWith("{")) {
    const parsed = JSON.parse(trimmed);
    if (!parsed || typeof parsed !== "object") {
      throw new Error("Stepper JSON command must be an object");
    }
    if (!("jsonrpc" in parsed)) {
      parsed.jsonrpc = "2.0";
    }
    if (!("id" in parsed)) {
      parsed.id = `web-control-${nextStepperRequestId++}`;
    }
    if (!("params" in parsed)) {
      parsed.params = {};
    }
    return parsed;
  }

  const parts = trimmed.split(/\s+/);
  const [command = "", subcommand = ""] = parts;
  const numberAt = (index: number, fallback = 0) => {
    const value = Number(parts[index]);
    return Number.isFinite(value) ? value : fallback;
  };

  if (command === "help" || command === "status" || command === "stop" || command === "abort" || command === "bootsel") {
    return buildStepperJsonRpcRequest(command);
  }

  if (command === "clear-fault") {
    return buildStepperJsonRpcRequest("clear-fault");
  }

  if (command === "home") {
    const hz = numberAt(1, STEPPER_HOME_RATE_HZ);
    return buildStepperHomeCommand(hz);
  }

  if (command === "pos") {
    return buildStepperPositionCommand(numberAt(1, 50));
  }

  if (command === "pos-time") {
    return buildStepperTimedPositionCommand(numberAt(1, 50), numberAt(2, STEPPER_SEND_DELAY_MS));
  }

  if (command === "pos-speed") {
    return buildStepperJsonRpcRequest("move-percent-speed", {
      percent: clampPercent(numberAt(1, 50)),
      hz: Math.max(1, Math.round(numberAt(2, 1200))),
    });
  }

  if (command === "step") {
    return buildStepperJsonRpcRequest("move-step", {
      step: Math.max(0, Math.round(numberAt(1, 0))),
    });
  }

  if (command === "step-time") {
    return buildStepperJsonRpcRequest("move-step-time", {
      step: Math.max(0, Math.round(numberAt(1, 0))),
      duration_ms: Math.max(1, Math.round(numberAt(2, STEPPER_SEND_DELAY_MS))),
    });
  }

  if (command === "step-speed") {
    return buildStepperJsonRpcRequest("move-step-speed", {
      step: Math.max(0, Math.round(numberAt(1, 0))),
      hz: Math.max(1, Math.round(numberAt(2, 1200))),
    });
  }

  if (command === "adsr") {
    return buildStepperAdsrCommand({
      attackPercent: numberAt(1, 100),
      attackMs: numberAt(2, 180),
      decayPercent: numberAt(3, 50),
      decayMs: numberAt(4, 220),
      sustainMs: numberAt(5, 0),
      releasePercent: numberAt(6, 50),
      releaseMs: numberAt(7, 320),
    });
  }

  if (command === "release") {
    return buildStepperReleaseCommand(numberAt(1, 50), numberAt(2, 320));
  }

  if (command === "current" && subcommand === "status") {
    return buildStepperJsonRpcRequest("current.status");
  }

  if (command === "current") {
    return buildStepperJsonRpcRequest("current.set", {
      which: subcommand,
      value: Math.max(0, Math.round(numberAt(2, 0))),
    });
  }

  if (command === "tmc" && subcommand === "test") {
    return buildStepperJsonRpcRequest("tmc.test");
  }

  if (command === "tmc" && subcommand === "read") {
    return buildStepperJsonRpcRequest("tmc.read", {
      reg: Number.parseInt(parts[2] || "0", 0),
    });
  }

  if (command === "tmc" && subcommand === "write") {
    return buildStepperJsonRpcRequest("tmc.write", {
      reg: Number.parseInt(parts[2] || "0", 0),
      value: Number.parseInt(parts[3] || "0", 0),
      verify: parts[4] !== "noverify",
    });
  }

  if (command === "tmc" && subcommand === "raw") {
    return buildStepperJsonRpcRequest("tmc.raw", {
      bytes: parts.slice(2).map((part) => Number.parseInt(part, 0)),
    });
  }

  if (command === "speed" && subcommand === "status") {
    return buildStepperJsonRpcRequest("speed.status");
  }

  if (command === "speed-test") {
    return buildStepperJsonRpcRequest("speed-test", {
      start_hz: Math.max(1, Math.round(numberAt(1, 400))),
      end_hz: Math.max(1, Math.round(numberAt(2, 1200))),
      step_hz: Math.max(1, Math.round(numberAt(3, 100))),
      repeats: Math.max(1, Math.round(numberAt(4, 2))),
    });
  }

  return buildStepperJsonRpcRequest(command, {});
}

export async function writeStepperTextCommand(
  port: WritableSerialPortLike | null,
  command: string | Record<string, unknown>,
  onTx?: (command: string) => void,
) {
  if (!port?.writable) {
    throw new Error("Stepper port is not connected");
  }

  const line = typeof command === "string" ? command : JSON.stringify(command);
  onTx?.(line);
  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${line}\n`));
  } finally {
    writer.releaseLock();
  }
}

export async function readStepperTextLines(
  port: ReadableSerialPortLike,
  {
    isActive,
    onReader,
    onRx,
    onLine,
    onError,
  }: {
    isActive: (port: ReadableSerialPortLike) => boolean;
    onReader?: (reader: unknown | null) => void;
    onRx?: (payload: string) => void;
    onLine: (line: string) => void;
    onError?: (error: unknown) => void;
  },
) {
  const decoder = new TextDecoder();
  let buffered = "";

  while (isActive(port) && port.readable) {
    const reader = port.readable.getReader();
    onReader?.(reader);
    try {
      while (isActive(port)) {
        const { value, done } = await reader.read();
        if (done) {
          break;
        }

        const chunk = decoder.decode(value, { stream: true });
        if (chunk.length > 0) {
          onRx?.(chunk);
        }

        buffered += chunk;
        const lines = buffered.split(/\r?\n/);
        buffered = lines.pop() || "";
        for (const rawLine of lines) {
          const line = rawLine.trim();
          if (!line || line === ">" || line.startsWith(">")) {
            continue;
          }
          onLine(line);
        }
      }
    } catch (error) {
      if (isActive(port)) {
        onError?.(error);
      }
    } finally {
      onReader?.(null);
      reader.releaseLock();
    }
    break;
  }
}
