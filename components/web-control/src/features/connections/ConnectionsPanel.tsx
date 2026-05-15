import { ConnectionCard } from "./ConnectionCard";
import type { Channel, Connection, MappingTarget } from "./connection-model";

export function ConnectionsPanel({
  activeNodeIds,
  connections,
  mappingTarget,
  serialSupported,
  onAddChannel,
  onConnect,
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
  connections: Connection[];
  mappingTarget: MappingTarget;
  serialSupported: boolean;
  onAddChannel: (connection: Connection) => void;
  onConnect: (connection: Connection) => void;
  onDelete: (connection: Connection) => void;
  onDisconnect: (connection: Connection) => void;
  onHome: (connection: Connection, channel: Channel) => void;
  onMap: (connection: Connection, channel: Channel) => void;
  onPositionCommit: (connection: Connection, channel: Channel, positionPercent: number) => void;
  onPositionInput: (connection: Connection, channel: Channel, positionPercent: number) => void;
  onResetFault: (connection: Connection, channel: Channel) => void;
  onRemoveChannel: (connection: Connection) => void;
  onRename: (connection: Connection, name: string) => void;
  onToggleConfig: (connection: Connection) => void;
}) {
  return (
    <>
      {connections.map((connection) => (
        <ConnectionCard
          activeNodeIds={activeNodeIds}
          connection={connection}
          key={connection.id}
          mappingTarget={mappingTarget}
          serialSupported={serialSupported}
          onAddChannel={() => onAddChannel(connection)}
          onConnect={() => onConnect(connection)}
          onDelete={() => onDelete(connection)}
          onDisconnect={() => onDisconnect(connection)}
          onHome={(channel) => onHome(connection, channel)}
          onMap={(channel) => onMap(connection, channel)}
          onPositionCommit={(channel, positionPercent) =>
            onPositionCommit(connection, channel, positionPercent)
          }
          onPositionInput={(channel, positionPercent) =>
            onPositionInput(connection, channel, positionPercent)
          }
          onResetFault={(channel) => onResetFault(connection, channel)}
          onRemoveChannel={() => onRemoveChannel(connection)}
          onRename={(name) => onRename(connection, name)}
          onToggleConfig={() => onToggleConfig(connection)}
        />
      ))}
    </>
  );
}
