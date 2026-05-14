import { ActionButton } from "../../components/ui/common";
import { ChannelRow } from "./ChannelRow";
import type { Channel, Connection, MappingTarget } from "./connection-model";

function ConnectionHeader({
  connection,
  mappedCount,
  serialSupported,
  onConnect,
  onDisconnect,
  onToggleConfig,
}: {
  connection: Connection;
  mappedCount: number;
  serialSupported: boolean;
  onConnect: () => void;
  onDisconnect: () => void;
  onToggleConfig: () => void;
}) {
  const connected = connection.port !== null;

  return (
    <div className="connection-card-header">
      <div className="connection-title">
        <div className="connection-name">{connection.name}</div>
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
      <ActionButton onClick={onToggleConfig}>Config</ActionButton>
    </div>
  );
}

export function ConnectionCard({
  activeNodeIds,
  connection,
  mappingTarget,
  serialSupported,
  onConnect,
  onDisconnect,
  onHome,
  onMap,
  onPositionCommit,
  onPositionInput,
  onToggleConfig,
}: {
  activeNodeIds: Set<string>;
  connection: Connection;
  mappingTarget: MappingTarget;
  serialSupported: boolean;
  onConnect: () => void;
  onDisconnect: () => void;
  onHome: (channel: Channel) => void;
  onMap: (channel: Channel) => void;
  onPositionCommit: (channel: Channel, positionPercent: number) => void;
  onPositionInput: (channel: Channel, positionPercent: number) => void;
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
            />
          ))}
        </div>
      ) : null}
    </div>
  );
}
