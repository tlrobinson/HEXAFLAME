import { Section } from "../../components/ui/Section";
import { ConnectionsPanel } from "../connections/ConnectionsPanel";
import { useConnectionsPanel } from "../connections/connections-store";
import { useMidiConnection } from "../midi/midi-store";

export function ConnectionsSection() {
  const { callbacks, snapshot } = useConnectionsPanel();
  const { callbacks: midiCallbacks, snapshot: midiSnapshot } = useMidiConnection();

  return (
    <Section label="Connections">
      <div className="section-row">
        <button
          className={`connection-button${midiSnapshot.connected ? " connected" : ""}`}
          disabled={!midiSnapshot.supported}
          type="button"
          onClick={midiCallbacks.onConnect}
        >
          <span className="connection-label">MIDI</span>
          <span
            aria-hidden="true"
            className="connection-close"
            onClick={(event) => {
              event.stopPropagation();
              midiCallbacks.onDisconnect();
            }}
          >
            ×
          </span>
        </button>
        <span className="status-text">{midiSnapshot.status}</span>
      </div>
      <div className="connections-list">
        <ConnectionsPanel
          activeNodeIds={snapshot.activeNodeIds}
          connections={snapshot.connections}
          mappingTarget={snapshot.mappingTarget}
          serialSupported={snapshot.serialSupported}
          onConnect={callbacks.onConnect}
          onDisconnect={callbacks.onDisconnect}
          onHome={callbacks.onHome}
          onMap={callbacks.onMap}
          onPositionCommit={callbacks.onPositionCommit}
          onPositionInput={callbacks.onPositionInput}
          onToggleConfig={callbacks.onToggleConfig}
        />
      </div>
      <div className="section-row-wrap">
        <button className="btn-compact" type="button" onClick={callbacks.onAddRelay}>Add Relay</button>
        <button className="btn-compact" type="button" onClick={callbacks.onAddStepper}>Add Stepper</button>
      </div>
    </Section>
  );
}
