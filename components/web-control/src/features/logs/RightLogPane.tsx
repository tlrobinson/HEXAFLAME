import { useSyncExternalStore } from "react";
import { DeviceLogSection } from "./DeviceLogSection";
import { LogCommandForm } from "./LogCommandForm";
import {
  clearDeviceLogs,
  getLogPaneSnapshot,
  setLogPaneCollapsed,
  subscribeToLogPane,
} from "./log-store";

export function RightLogPane() {
  const snapshot = useSyncExternalStore(
    subscribeToLogPane,
    getLogPaneSnapshot,
    getLogPaneSnapshot,
  );
  const { commandEnabled, collapsed, logs } = snapshot;

  return (
    <aside className={`log-pane${collapsed ? " collapsed" : ""}`} id="log-pane">
      <button
        aria-expanded={!collapsed}
        aria-label="Toggle device logs"
        className="log-pane-toggle"
        onClick={() => setLogPaneCollapsed(!collapsed)}
        title="Toggle device logs"
        type="button"
      >
        ⚙
      </button>
      <div className="log-pane-header">
        <div className="log-pane-title-row">
          <h2>Device Logs</h2>
          <button className="btn-compact" type="button" onClick={clearDeviceLogs}>Clear</button>
        </div>
        <div className="log-pane-subtitle">Inbound and outbound messages by device</div>
      </div>
      <div className="log-pane-body">
        <DeviceLogSection
          emptyText="No MIDI traffic yet."
          entries={logs.midi}
          title="MIDI"
          titleId="midi-log-title"
        />
        <DeviceLogSection
          emptyText="No relay traffic yet."
          entries={logs.relay}
          title="Relay"
          titleId="relay-log-title"
        >
          <LogCommandForm
            buttonId="relay-command-send-button"
            disabled={!commandEnabled.relay}
            formId="relay-command-form"
            inputId="relay-command-input"
            placeholder="hex: 01 05 00 00 ff 00"
            role="relay"
          />
        </DeviceLogSection>
        <DeviceLogSection
          emptyText="No stepper traffic yet."
          entries={logs.stepper}
          title="Stepper"
          titleId="stepper-log-title"
        >
          <LogCommandForm
            buttonId="stepper-command-send-button"
            disabled={!commandEnabled.stepper}
            formId="stepper-command-form"
            inputId="stepper-command-input"
            placeholder='status, home 400, pos-time 50 250, or {"method":"status"}'
            role="stepper"
          />
        </DeviceLogSection>
      </div>
    </aside>
  );
}
