import { ConnectionCard } from "./ConnectionCard";
import type { Channel, Connection, MappingTarget } from "./connection-model";

export function ConnectionsPanel({
  activeNodeIds,
  connections,
  mappingTarget,
  serialSupported,
  onConnect,
  onDelete,
  onDisconnect,
  onHome,
  onMap,
  onPositionCommit,
  onPositionInput,
  onRename,
  onToggleConfig,
}: {
  activeNodeIds: Set<string>;
  connections: Connection[];
  mappingTarget: MappingTarget;
  serialSupported: boolean;
  onConnect: (connection: Connection) => void;
  onDelete: (connection: Connection) => void;
  onDisconnect: (connection: Connection) => void;
  onHome: (connection: Connection, channel: Channel) => void;
  onMap: (connection: Connection, channel: Channel) => void;
  onPositionCommit: (connection: Connection, channel: Channel, positionPercent: number) => void;
  onPositionInput: (connection: Connection, channel: Channel, positionPercent: number) => void;
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
          onRename={(name) => onRename(connection, name)}
          onToggleConfig={() => onToggleConfig(connection)}
        />
      ))}
    </>
  );
}
