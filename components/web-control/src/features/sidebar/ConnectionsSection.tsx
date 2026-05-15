import { useMemo, useState, type FormEvent } from "react";
import { ActionButton } from "../../components/ui/common";
import { Section } from "../../components/ui/Section";
import { ConnectionsPanel } from "../connections/ConnectionsPanel";
import { DeviceNameEditor } from "../connections/DeviceNameEditor";
import type { AddDeviceRequest } from "../connections/connections-store";
import { useConnectionsPanel } from "../connections/connections-store";
import { setMidiDeviceName, useMidiConnection } from "../midi/midi-store";
import { getMidiInputLabel, type MidiAccessLike, type MidiInputLike } from "../../devices/midi";
import { getSerialPortKey, getSerialPortLabel } from "../../devices/relay";

type AddDeviceType = AddDeviceRequest["type"];

type NavigatorWithSerial = Navigator & {
  serial?: {
    requestPort(): Promise<unknown>;
  };
};

type NavigatorWithMidi = Navigator & {
  requestMIDIAccess?: (options?: { sysex?: boolean }) => Promise<MidiAccessLike>;
};

type MidiInputOption = {
  id: string;
  label: string;
  state?: string;
};

function defaultNameFor(type: AddDeviceType, suffix: number) {
  if (type === "midi") {
    return "MIDI";
  }
  return `${type === "relay" ? "Relay" : "Stepper"} ${suffix}`;
}

export function ConnectionsSection() {
  const { callbacks, snapshot } = useConnectionsPanel();
  const { callbacks: midiCallbacks, snapshot: midiSnapshot } = useMidiConnection();
  const [modalOpen, setModalOpen] = useState(false);
  const [deviceType, setDeviceType] = useState<AddDeviceType>("relay");
  const [deviceName, setDeviceName] = useState("");
  const [midiInputId, setMidiInputId] = useState("all");
  const [midiInputs, setMidiInputs] = useState<MidiInputOption[]>([]);
  const [midiInputStatus, setMidiInputStatus] = useState("Select all MIDI inputs or choose a specific input.");
  const [portKey, setPortKey] = useState<string | null>(null);
  const [portLabel, setPortLabel] = useState("");
  const serialSupported = Boolean((navigator as NavigatorWithSerial).serial);
  const midiSupported = Boolean((navigator as NavigatorWithMidi).requestMIDIAccess);
  const suggestedName = useMemo(
    () => defaultNameFor(deviceType, snapshot.connections.length + 1),
    [deviceType, snapshot.connections.length],
  );

  function renameMidiDevice(nextName: string) {
    if (nextName) {
      setMidiDeviceName(nextName);
      midiCallbacks.onRename(nextName);
    }
  }

  function openAddModal() {
    setDeviceType("relay");
    setDeviceName(defaultNameFor("relay", snapshot.connections.length + 1));
    setMidiInputId("all");
    setMidiInputStatus("Select all MIDI inputs or choose a specific input.");
    setPortKey(null);
    setPortLabel("");
    setModalOpen(true);
  }

  function handleTypeChange(type: AddDeviceType) {
    setDeviceType(type);
    setDeviceName(defaultNameFor(type, snapshot.connections.length + 1));
    setMidiInputId("all");
    setMidiInputStatus("Select all MIDI inputs or choose a specific input.");
    setPortKey(null);
    setPortLabel("");
    if (type === "midi") {
      void loadMidiInputs();
    }
  }

  async function loadMidiInputs() {
    if (!midiSupported) {
      setMidiInputs([]);
      setMidiInputStatus("Web MIDI unsupported");
      return;
    }

    setMidiInputStatus("Loading MIDI inputs...");
    try {
      const access = await (navigator as NavigatorWithMidi).requestMIDIAccess?.({
        sysex: false,
      });
      const inputs = [...(access?.inputs.values() || [])].map((input: MidiInputLike) => ({
        id: input.id,
        label: getMidiInputLabel(input),
        state: input.state,
      }));
      setMidiInputs(inputs);
      setMidiInputStatus(
        inputs.length > 0
          ? "Choose a MIDI input, or use all inputs."
          : "No MIDI inputs found",
      );
    } catch (error) {
      console.error(error);
      setMidiInputs([]);
      setMidiInputStatus("MIDI access denied");
    }
  }

  function handleMidiInputChange(inputId: string) {
    setMidiInputId(inputId);
    if (inputId === "all") {
      setDeviceName("MIDI");
      return;
    }

    const input = midiInputs.find((candidate) => candidate.id === inputId);
    if (input) {
      setDeviceName(input.label);
    }
  }

  async function chooseSerialPort() {
    try {
      const port = await (navigator as NavigatorWithSerial).serial?.requestPort();
      const nextPortKey = port ? getSerialPortKey(port) : null;
      const nextPortLabel = port ? getSerialPortLabel(port) : "";
      setPortKey(nextPortKey);
      setPortLabel(nextPortLabel);
      if (nextPortLabel) {
        setDeviceName(nextPortLabel);
      }
    } catch (error) {
      console.error(error);
    }
  }

  function handleAddDevice(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    const name = deviceName.trim() || suggestedName;
    if (deviceType === "midi") {
      callbacks.onAddDevice({ name, type: "midi" });
    } else {
      callbacks.onAddDevice({ name, portKey, type: deviceType });
    }
    setModalOpen(false);
  }

  return (
    <Section
      label="Devices"
      actions={
        <button className="btn-compact" type="button" onClick={openAddModal}>
          Add Device
        </button>
      }
    >
      <div className="connections-list">
        {midiSnapshot.enabled ? (
        <div className="connection-card">
          <div className="connection-card-header">
            <div className="connection-title">
              <div className="connection-name-row">
                <span
                  aria-label={midiSnapshot.connected ? "Connected" : "Disconnected"}
                  className={`connection-state-dot${midiSnapshot.connected ? " connected" : ""}`}
                  role="img"
                />
                <DeviceNameEditor name={midiSnapshot.deviceName} onRename={renameMidiDevice} />
              </div>
              <div className="connection-meta">{midiSnapshot.status}</div>
            </div>
            <ActionButton
              className={`connection-button${midiSnapshot.connected ? " connected" : ""}`}
              disabled={!midiSnapshot.supported}
              onClick={midiSnapshot.connected ? midiCallbacks.onDisconnect : midiCallbacks.onConnect}
            >
              {midiSnapshot.connected ? "Disconnect" : "Connect"}
            </ActionButton>
            <ActionButton onClick={midiCallbacks.onToggleConfig}>
              {midiSnapshot.expanded ? "Close" : "Edit"}
            </ActionButton>
          </div>
          {midiSnapshot.expanded ? (
            <div className="connection-channels">
              {midiSnapshot.mappings.map((mapping) => {
                const isLearning = midiSnapshot.learningMappingId === mapping.id;
                const isMapping = midiSnapshot.mappingTargetId === mapping.id;
                const active =
                  mapping.note !== null && midiSnapshot.heldNotes.has(mapping.note);
                return (
                  <div className="channel-row" key={mapping.id}>
                    <div className="channel-topline">
                      <div className="channel-heading-text">
                        <span
                          aria-label={active ? "Active" : "Inactive"}
                          className={`midi-note-state-dot${active ? " active" : ""}`}
                          role="img"
                        />
                        <span className="channel-title">
                          {mapping.note === null ? "Note ?" : `Note ${mapping.note}`}
                        </span>
                        <button
                          className="channel-map-target"
                          type="button"
                          onClick={() => midiCallbacks.onMap(mapping)}
                        >
                          {isLearning
                            ? "Press a note..."
                            : isMapping
                              ? "Click a jet..."
                              : mapping.jetId || "Unmapped"}
                        </button>
                      </div>
                      <div className="channel-actions">
                        <ActionButton onClick={() => midiCallbacks.onRemoveMapping(mapping)}>
                          Remove
                        </ActionButton>
                      </div>
                    </div>
                  </div>
                );
              })}
              <div className="connection-card-actions">
                <button className="btn-compact" type="button" onClick={midiCallbacks.onAddMapping}>
                  Learn
                </button>
                <button className="btn-danger-compact" type="button" onClick={midiCallbacks.onDelete}>
                  Delete
                </button>
              </div>
            </div>
          ) : null}
        </div>
        ) : null}
        <ConnectionsPanel
          activeNodeIds={snapshot.activeNodeIds}
          connections={snapshot.connections}
          mappingTarget={snapshot.mappingTarget}
          serialSupported={snapshot.serialSupported}
          onConnect={callbacks.onConnect}
          onDelete={callbacks.onDelete}
          onDisconnect={callbacks.onDisconnect}
          onHome={callbacks.onHome}
          onMap={callbacks.onMap}
          onPositionCommit={callbacks.onPositionCommit}
          onPositionInput={callbacks.onPositionInput}
          onRename={callbacks.onRename}
          onToggleConfig={callbacks.onToggleConfig}
        />
      </div>
      {modalOpen ? (
        <div className="modal-backdrop" role="presentation">
          <form className="device-modal" onSubmit={handleAddDevice}>
            <div className="modal-header">
              <h3>Add Device</h3>
              <button
                aria-label="Close"
                className="btn-compact"
                type="button"
                onClick={() => setModalOpen(false)}
              >
                ×
              </button>
            </div>
            <label>
              Device type
              <select
                value={deviceType}
                onChange={(event) => handleTypeChange(event.currentTarget.value as AddDeviceType)}
              >
                <option value="relay">Relay</option>
                <option value="stepper">Stepper</option>
                <option value="midi">MIDI</option>
              </select>
            </label>
            <label>
              Device name
              <input
                type="text"
                value={deviceName}
                placeholder={suggestedName}
                onChange={(event) => setDeviceName(event.currentTarget.value)}
              />
            </label>
            {deviceType === "midi" ? (
              <div className="modal-field-block">
                <div className="modal-field-label">MIDI input</div>
                <select
                  disabled={!midiSupported}
                  value={midiInputId}
                  onChange={(event) => handleMidiInputChange(event.currentTarget.value)}
                >
                  <option value="all">All inputs</option>
                  {midiInputs.map((input) => (
                    <option key={input.id} value={input.id}>
                      {input.label}{input.state ? ` (${input.state})` : ""}
                    </option>
                  ))}
                </select>
                <div className="section-row-wrap">
                  <button
                    className="btn-compact"
                    disabled={!midiSupported}
                    type="button"
                    onClick={loadMidiInputs}
                  >
                    Refresh
                  </button>
                  <span className="status-text">{midiInputStatus}</span>
                </div>
              </div>
            ) : (
              <div className="modal-field-block">
                <div className="modal-field-label">Serial port</div>
                <div className="section-row-wrap">
                  <button
                    className="btn-compact"
                    disabled={!serialSupported}
                    type="button"
                    onClick={chooseSerialPort}
                  >
                    Choose Port
                  </button>
                  <span className="status-text">
                    {portLabel || portKey || (serialSupported ? "No port selected" : "Web Serial unsupported")}
                  </span>
                </div>
              </div>
            )}
            <div className="modal-actions">
              <button type="button" onClick={() => setModalOpen(false)}>Cancel</button>
              <button className="btn-accent" type="submit">Add</button>
            </div>
          </form>
        </div>
      ) : null}
    </Section>
  );
}
