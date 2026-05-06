export const STEPPER_SERIAL_BAUD = 115200;
export const STEPPER_SEND_DELAY_MS = 75;
export const STEPPER_ENVELOPE_SEND_DELAY_MS = 20;
export const STEPPER_HOME_RATE_HZ = 400;

export interface StepperProtocolUpdate {
  statusMessage: string;
  homed?: boolean;
  travelSteps?: number | null;
  positionPercent?: number;
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

export function buildStepperPositionCommand(percent: number) {
  return `pos ${percent.toFixed(1)}`;
}

export function buildStepperHomeCommand(rateHz = STEPPER_HOME_RATE_HZ) {
  return `home ${rateHz}`;
}

export function parseStepperProtocolLine(line: string): StepperProtocolUpdate {
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

  return { statusMessage: line };
}

export async function writeStepperTextCommand(
  port: WritableSerialPortLike | null,
  command: string,
  onTx?: (command: string) => void,
) {
  if (!port?.writable) {
    throw new Error("Stepper port is not connected");
  }

  onTx?.(command);
  const writer = port.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${command}\n`));
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
