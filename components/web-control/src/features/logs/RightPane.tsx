import {
  useEffect,
  useRef,
  useState,
  useSyncExternalStore,
  type FormEvent,
  type KeyboardEvent,
  type PointerEvent as ReactPointerEvent,
  type ReactNode,
} from "react";
import { formatLogTime } from "./log-format";
import {
  clearDeviceLogs,
  getLogPaneSnapshot,
  sendDeviceCommand,
  setCommandDevice,
  setLogFilters,
  setLogPaneCollapsed,
  setLogPaneTab,
  setLogPaneWidth,
  subscribeToLogPane,
  type DeviceCommandRole,
  type DeviceLogEntry,
  type DeviceLogRole,
  type DeviceLogDirection,
} from "./log-store";
import { useScriptEditor } from "../sidebar/script-editor-store";

const deviceOptions: Array<DeviceLogRole | "all"> = ["all", "midi", "relay", "stepper"];
const typeOptions: Array<DeviceLogDirection | "all"> = ["all", "event", "rx", "tx"];
const commandDevices: DeviceCommandRole[] = ["relay", "stepper"];

function JsonToken({
  children,
  kind,
}: {
  children: ReactNode;
  kind: "boolean" | "key" | "null" | "number" | "string";
}) {
  return <span className={`json-token json-${kind}`}>{children}</span>;
}

function renderJsonValue(value: unknown): ReactNode {
  if (value === null) {
    return <JsonToken kind="null">null</JsonToken>;
  }
  if (typeof value === "string") {
    return <JsonToken kind="string">{JSON.stringify(value)}</JsonToken>;
  }
  if (typeof value === "number") {
    return <JsonToken kind="number">{String(value)}</JsonToken>;
  }
  if (typeof value === "boolean") {
    return <JsonToken kind="boolean">{String(value)}</JsonToken>;
  }
  if (Array.isArray(value)) {
    return (
      <>
        [
        {value.map((item, index) => (
          <span key={index}>
            {index > 0 ? ", " : ""}
            {renderJsonValue(item)}
          </span>
        ))}
        ]
      </>
    );
  }
  if (typeof value === "object") {
    return (
      <>
        {"{"}
        {Object.entries(value as Record<string, unknown>).map(([key, item], index) => (
          <span key={key}>
            {index > 0 ? ", " : ""}
            <JsonToken kind="key">{JSON.stringify(key)}</JsonToken>: {renderJsonValue(item)}
          </span>
        ))}
        {"}"}
      </>
    );
  }
  return String(value);
}

function LogMessage({ payload }: { payload: string }) {
  try {
    return <>{renderJsonValue(JSON.parse(payload))}</>;
  } catch {
    return <>{payload}</>;
  }
}

function LogRow({ entry }: { entry: DeviceLogEntry }) {
  return (
    <div className="log-entry">
      <span className="log-time">{formatLogTime(entry.timestampMs)}</span>
      <span className={`log-device ${entry.device}`}>{entry.device}</span>
      <span className={`log-dir ${entry.direction}`}>{entry.direction.toUpperCase()}</span>
      <span className="log-payload"><LogMessage payload={entry.payload} /></span>
    </div>
  );
}

function CommandBar({
  commandDevice,
  disabled,
}: {
  commandDevice: DeviceCommandRole;
  disabled: boolean;
}) {
  const [value, setValue] = useState("");

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const sent = await sendDeviceCommand(commandDevice, value);
    if (sent) {
      setValue("");
    }
  }

  return (
    <form className="log-command-bar" onSubmit={handleSubmit}>
      <label className="log-command-device">
        Device
        <select
          value={commandDevice}
          onChange={(event) => setCommandDevice(event.currentTarget.value as DeviceCommandRole)}
        >
          {commandDevices.map((device) => (
            <option key={device} value={device}>{device}</option>
          ))}
        </select>
      </label>
      <div className="command-row">
        <input
          autoComplete="off"
          className="command-input"
          disabled={disabled}
          onChange={(event) => setValue(event.target.value)}
          placeholder={
            commandDevice === "relay"
              ? "hex: 01 05 00 00 ff 00"
              : 'status, home 400, pos-time 50 250, or {"method":"status"}'
          }
          type="text"
          value={value}
        />
        <button className="btn-compact" disabled={disabled} type="submit">Send</button>
      </div>
    </form>
  );
}

function ScriptEditorPane() {
  const { callbacks, snapshot } = useScriptEditor();

  function handleKeyDown(event: KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key !== "Tab") {
      return;
    }
    event.preventDefault();
    const target = event.currentTarget;
    const start = target.selectionStart;
    const end = target.selectionEnd;
    const nextScript =
      snapshot.script.substring(0, start) +
      "  " +
      snapshot.script.substring(end);
    callbacks.onChange(nextScript);
    window.requestAnimationFrame(() => {
      target.selectionStart = target.selectionEnd = start + 2;
    });
  }

  return (
    <div className="editor-pane">
      <textarea
        className="animation-editor right-pane-editor"
        rows={12}
        spellCheck={false}
        value={snapshot.script}
        onChange={(event) => callbacks.onChange(event.currentTarget.value)}
        onKeyDown={handleKeyDown}
      />
      <div className="editor-actions">
        <button type="button" onClick={callbacks.onRestore}>Restore</button>
        <button type="button" onClick={callbacks.onToggleHelp}>?</button>
      </div>
      <div className="script-help" style={{ display: snapshot.helpOpen ? "" : "none" }}>
        <h3>Script Reference</h3>
        <dl>
          <dt>Selectors</dt>
          <dd><code>all</code> &mdash; every node</dd>
          <dd><code>center</code> &mdash; origin node</dd>
          <dd><code>type:center</code> / <code>type:vertex</code></dd>
          <dd><code>dist:N</code> &mdash; nodes at distance N</dd>
          <dd><code>dist:N..M</code> &mdash; distance range</dd>
          <dt>Variables</dt>
          <dd><code>$d</code> &mdash; dist loop index</dd>
          <dd><code>$max</code> &mdash; max distance</dd>
          <dd><code>$n</code> &mdash; current node in for-node</dd>
          <dd>Arithmetic: <code>$d+1</code> <code>$d-1</code> <code>$max-1</code></dd>
          <dt>Frame Commands</dt>
          <dd><code>clear</code> / <code>add</code> / <code>remove</code> / <code>set</code> / <code>frame</code></dd>
          <dt>Loops</dt>
          <dd><code>for-dist [from] [to]</code> &mdash; ascending</dd>
          <dd><code>for-dist-rev [from] [to]</code> &mdash; descending</dd>
          <dd><code>for-node &lt;sel&gt; [sort angle|shell-angle]</code></dd>
          <dd><code>end</code></dd>
          <dt>Walk</dt>
          <dd><code>cursor &lt;sel&gt;</code></dd>
          <dd><code>walk-to $n [constrain &lt;sel&gt;]</code></dd>
          <dd><code>dfs [sort shell-angle]</code></dd>
          <dt>Filter</dt>
          <dd><code>filter-scene type:vertex</code></dd>
        </dl>
        <p><code># comments</code> are ignored</p>
      </div>
      {snapshot.error ? (
        <div className="editor-error">{snapshot.error}</div>
      ) : null}
    </div>
  );
}

export function RightPane() {
  const snapshot = useSyncExternalStore(
    subscribeToLogPane,
    getLogPaneSnapshot,
    getLogPaneSnapshot,
  );
  const streamRef = useRef<HTMLDivElement | null>(null);
  const { activeTab, collapsed, commandDevice, commandEnabled, filters, logs, width } = snapshot;
  const filteredLogs = logs.filter((entry) => {
    const deviceMatches = filters.device === "all" || entry.device === filters.device;
    const typeMatches = filters.type === "all" || entry.direction === filters.type;
    return deviceMatches && typeMatches;
  });

  useEffect(() => {
    const stream = streamRef.current;
    if (stream) {
      stream.scrollTop = stream.scrollHeight;
    }
  }, [filteredLogs.length]);

  function handleResizePointerDown(event: ReactPointerEvent<HTMLDivElement>) {
    event.preventDefault();
    const startX = event.clientX;
    const startWidth = width;

    function handlePointerMove(moveEvent: PointerEvent) {
      setLogPaneWidth(startWidth + startX - moveEvent.clientX);
    }

    function handlePointerUp() {
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
    }

    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp, { once: true });
  }

  return (
    <aside
      className={`right-pane log-pane${collapsed ? " collapsed" : ""}`}
      id="log-pane"
      style={{
        marginRight: collapsed ? -width : 0,
        width,
      }}
    >
      <div
        aria-hidden="true"
        className="pane-resize-handle pane-resize-handle-right"
        onPointerDown={handleResizePointerDown}
      />
      <button
        aria-expanded={!collapsed}
        aria-label="Toggle device logs"
        className="log-pane-toggle"
        style={{ right: collapsed ? 10 : width + 10 }}
        onClick={() => setLogPaneCollapsed(!collapsed)}
        title="Toggle device logs"
        type="button"
      >
        ⚙
      </button>
      <div className="log-pane-header">
        <div className="log-pane-title-row">
          <h2>{activeTab === "logs" ? "Device Logs" : "Script Editor"}</h2>
          {activeTab === "logs" ? (
            <button className="btn-compact" type="button" onClick={clearDeviceLogs}>Clear</button>
          ) : (
            <span className="btn-compact pane-header-action-placeholder" aria-hidden="true">
              Clear
            </span>
          )}
        </div>
        <div className="right-pane-tabs" role="tablist" aria-label="Right pane views">
          <button
            className={activeTab === "logs" ? "active" : ""}
            role="tab"
            type="button"
            aria-selected={activeTab === "logs"}
            onClick={() => setLogPaneTab("logs")}
          >
            Logs
          </button>
          <button
            className={activeTab === "editor" ? "active" : ""}
            role="tab"
            type="button"
            aria-selected={activeTab === "editor"}
            onClick={() => setLogPaneTab("editor")}
          >
            Editor
          </button>
        </div>
        {activeTab === "logs" ? (
          <div className="log-filter-row">
            <label>
              Device
              <select
                value={filters.device}
                onChange={(event) =>
                  setLogFilters({ device: event.currentTarget.value as DeviceLogRole | "all" })
                }
              >
                {deviceOptions.map((device) => (
                  <option key={device} value={device}>{device}</option>
                ))}
              </select>
            </label>
            <label>
              Type
              <select
                value={filters.type}
                onChange={(event) =>
                  setLogFilters({ type: event.currentTarget.value as DeviceLogDirection | "all" })
                }
              >
                {typeOptions.map((type) => (
                  <option key={type} value={type}>{type}</option>
                ))}
              </select>
            </label>
          </div>
        ) : null}
      </div>
      <div className="log-pane-body">
        {activeTab === "logs" ? (
          <div className="device-log-stream unified-log-stream" ref={streamRef} aria-live="polite">
            {filteredLogs.length === 0 ? (
              <div className="log-empty">No matching log entries.</div>
            ) : (
              filteredLogs.map((entry) => <LogRow entry={entry} key={entry.id} />)
            )}
          </div>
        ) : (
          <ScriptEditorPane />
        )}
      </div>
      {activeTab === "logs" ? (
        <CommandBar
          commandDevice={commandDevice}
          disabled={!commandEnabled[commandDevice]}
        />
      ) : null}
    </aside>
  );
}
