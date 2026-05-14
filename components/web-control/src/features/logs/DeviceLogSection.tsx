import { useEffect, useRef, type ReactNode } from "react";
import { formatLogTime } from "./log-format";
import type { DeviceLogEntry } from "./log-store";

export function DeviceLogSection({
  children,
  emptyText,
  entries,
  title,
  titleId,
}: {
  children?: ReactNode;
  emptyText: string;
  entries: DeviceLogEntry[];
  title: string;
  titleId: string;
}) {
  const streamRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const stream = streamRef.current;
    if (stream) {
      stream.scrollTop = stream.scrollHeight;
    }
  }, [entries]);

  return (
    <section className="device-log" aria-labelledby={titleId}>
      <div className="device-log-header">
        <div className="device-log-title" id={titleId}>{title}</div>
        <div className="device-log-count">
          {entries.length} {entries.length === 1 ? "entry" : "entries"}
        </div>
      </div>
      <div className="device-log-stream" ref={streamRef} aria-live="polite">
        {entries.length === 0 ? (
          <div className="log-empty">{emptyText}</div>
        ) : (
          entries.map((entry, index) => (
            <div className="log-entry" key={`${entry.timestampMs}:${index}`}>
              <span className="log-time">{formatLogTime(entry.timestampMs)}</span>
              <span className={`log-dir ${entry.direction}`}>
                {entry.direction.toUpperCase()}
              </span>
              <span className="log-payload">{entry.payload}</span>
            </div>
          ))
        )}
      </div>
      {children}
    </section>
  );
}
