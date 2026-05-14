export function formatSerialBytes(bytes: Iterable<number>) {
  return [...bytes]
    .map((value) => value.toString(16).padStart(2, "0"))
    .join(" ");
}

export function formatLogTime(timestampMs: number) {
  const date = new Date(timestampMs);
  return [
    date.getHours().toString().padStart(2, "0"),
    date.getMinutes().toString().padStart(2, "0"),
    date.getSeconds().toString().padStart(2, "0"),
  ].join(":");
}

export function formatLogPayload(payload: unknown) {
  if (typeof payload === "string") {
    const normalized = payload.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
    if (normalized.length === 0) {
      return "(empty)";
    }

    try {
      const parsed = JSON.parse(normalized);
      if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
        const { jsonrpc, ...withoutJsonRpc } = parsed;
        return JSON.stringify(withoutJsonRpc);
      }
    } catch {
      // Non-JSON log payloads are displayed as plain text.
    }

    return normalized;
  }

  if (payload instanceof Uint8Array || Array.isArray(payload)) {
    return formatSerialBytes(payload);
  }

  return String(payload);
}

export function splitLogPayload(payload: unknown) {
  if (typeof payload !== "string") {
    return [formatLogPayload(payload)];
  }

  const lines = payload
    .replace(/\r\n/g, "\n")
    .replace(/\r/g, "\n")
    .split("\n")
    .filter((line) => line.length > 0)
    .map(formatLogPayload);

  return lines.length > 0 ? lines : ["(empty)"];
}
