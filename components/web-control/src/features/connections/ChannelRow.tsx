import { ActionButton, MetricText } from "../../components/ui/common";
import type { Channel, Connection, MappingTarget } from "./connection-model";

function channelTargetLabel(channel: Channel) {
  return channel.jetId || "Unmapped";
}

export function ChannelRow({
  channel,
  connection,
  mappingTarget,
  active,
  onMap,
  onHome,
  onPositionInput,
  onPositionCommit,
}: {
  channel: Channel;
  connection: Connection;
  mappingTarget: MappingTarget;
  active: boolean;
  onMap: () => void;
  onHome: () => void;
  onPositionInput: (positionPercent: number) => void;
  onPositionCommit: (positionPercent: number) => void;
}) {
  const isMapping =
    mappingTarget?.connectionId === connection.id &&
    mappingTarget?.channelIndex === channel.index;
  const positionPercent = Number(channel.positionPercent ?? 50);

  return (
    <div className="channel-row">
      <div className="channel-topline">
        <span className="channel-title">Channel {channel.index + 1}</span>
        <span className="channel-map-target">
          {isMapping ? "Click a jet..." : channelTargetLabel(channel)}
        </span>
      </div>
      <div className="channel-actions">
        <ActionButton onClick={onMap}>Map</ActionButton>
        {connection.type === "stepper" ? (
          <ActionButton disabled={connection.port === null} onClick={onHome}>
            Home
          </ActionButton>
        ) : null}
      </div>
      {connection.type === "stepper" ? (
        <>
          <div className="channel-metrics">
            <MetricText>State: {channel.state || "Unknown"}</MetricText>
            <MetricText>
              {channel.travelSteps === null || channel.travelSteps === undefined
                ? "Travel: Unknown"
                : `Travel: ${Number(channel.travelSteps).toLocaleString()} steps`}
            </MetricText>
          </div>
          <div className="channel-position">
            <label>Position</label>
            <input
              disabled={connection.port === null || !channel.homed}
              max="100"
              min="0"
              step="0.1"
              type="range"
              value={positionPercent}
              onChange={(event) => onPositionInput(Number(event.currentTarget.value))}
              onKeyUp={(event) => onPositionCommit(Number(event.currentTarget.value))}
              onPointerUp={(event) => onPositionCommit(Number(event.currentTarget.value))}
            />
            <output>{positionPercent.toFixed(1)}%</output>
          </div>
        </>
      ) : (
        <div className="channel-metrics">
          <MetricText>State: {active ? "On" : "Off"}</MetricText>
        </div>
      )}
    </div>
  );
}
