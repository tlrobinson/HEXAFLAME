import { ActionButton } from "../../components/ui/common";
import { ChannelRow } from "./ChannelRow";
import type { Channel, Connection, MappingTarget } from "./connection-model";
import { DeviceNameEditor } from "./DeviceNameEditor";

function ConnectionHeader({
  connection,
  mappedCount,
  serialSupported,
  onConnect,
  onDisconnect,
  onRename,
  onToggleConfig,
}: {
  connection: Connection;
  mappedCount: number;
  serialSupported: boolean;
  onConnect: () => void;
  onDisconnect: () => void;
  onRename: (name: string) => void;
  onToggleConfig: () => void;
}) {
  const connected = connection.port !== null;

  return (
    <div className="connection-card-header">
      <div className="connection-title">
        <div className="connection-name-row">
          <span
            aria-label={connected ? "Connected" : "Disconnected"}
            className={`connection-state-dot${connected ? " connected" : ""}`}
            role="img"
          />
          <DeviceNameEditor name={connection.name} onRename={onRename} />
        </div>
        <div className="connection-meta">
          {connection.status} · {mappedCount}/{connection.channels.length} mapped
        </div>
      </div>
      <ActionButton
        className={`connection-button${connected ? " connected" : ""}`}
        disabled={!serialSupported || connection.connectInProgress}
        onClick={connected ? onDisconnect : onConnect}
      >
        {connected ? "Disconnect" : "Connect"}
      </ActionButton>
      <ActionButton onClick={onToggleConfig}>
        {connection.expanded ? "Close" : "Edit"}
      </ActionButton>
    </div>
  );
}

export function ConnectionCard({
  activeNodeIds,
  connection,
  mappingTarget,
  serialSupported,
  onConnect,
  onAddChannel,
  onDelete,
  onDisconnect,
  onHome,
  onMap,
  onPositionCommit,
  onPositionInput,
  onResetFault,
  onRemoveChannel,
  onRename,
  onToggleConfig,
}: {
  activeNodeIds: Set<string>;
  connection: Connection;
  mappingTarget: MappingTarget;
  serialSupported: boolean;
  onAddChannel: () => void;
  onConnect: () => void;
  onDelete: () => void;
  onDisconnect: () => void;
  onHome: (channel: Channel) => void;
  onMap: (channel: Channel) => void;
  onPositionCommit: (channel: Channel, positionPercent: number) => void;
  onPositionInput: (channel: Channel, positionPercent: number) => void;
  onResetFault: (channel: Channel) => void;
  onRemoveChannel: () => void;
  onRename: (name: string) => void;
  onToggleConfig: () => void;
}) {
  const mappedCount = connection.channels.filter((channel) => channel.jetId).length;

  return (
    <div className="connection-card">
      <ConnectionHeader
        connection={connection}
        mappedCount={mappedCount}
        serialSupported={serialSupported}
        onConnect={onConnect}
        onDisconnect={onDisconnect}
        onRename={onRename}
        onToggleConfig={onToggleConfig}
      />
      {connection.expanded ? (
        <div className="connection-channels">
          {connection.channels.map((channel) => (
            <ChannelRow
              active={Boolean(channel.jetId && activeNodeIds.has(channel.jetId))}
              channel={channel}
              connection={connection}
              key={`${connection.id}:${channel.index}`}
              mappingTarget={mappingTarget}
              onHome={() => onHome(channel)}
              onMap={() => onMap(channel)}
              onPositionCommit={(positionPercent) =>
                onPositionCommit(channel, positionPercent)
              }
              onPositionInput={(positionPercent) =>
                onPositionInput(channel, positionPercent)
              }
              onResetFault={() => onResetFault(channel)}
            />
          ))}
          <div className="connection-card-actions">
            <button className="btn-compact" type="button" onClick={onAddChannel}>
              Add Channel
            </button>
            <button
              className="btn-compact"
              disabled={connection.channels.length <= 1}
              type="button"
              onClick={onRemoveChannel}
            >
              Remove Channel
            </button>
            <button className="btn-danger-compact" type="button" onClick={onDelete}>
              Delete Device
            </button>
          </div>
        </div>
      ) : null}
    </div>
  );
}
