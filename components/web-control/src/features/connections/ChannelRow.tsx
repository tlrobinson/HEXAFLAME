import { MetricText } from "../../components/ui/common";
import type { Channel, Connection, MappingTarget } from "./connection-model";

function channelTargetLabel(channel: Channel) {
  return channel.jetId || "Unmapped";
}

function motionStateClassName(motionState: string | undefined) {
  const normalized = (motionState || "").toLowerCase();
  if (normalized.includes("moving") || normalized.includes("envelope")) {
    return " active";
  }
  if (normalized.includes("idle") || normalized.includes("ready")) {
    return " idle";
  }
  return "";
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
  const stepperLevel = Math.min(Math.max(positionPercent / 100, 0), 1);
  const dotActive = connection.type === "stepper" ? stepperLevel > 0 : active;
  const dotOpacity =
    connection.type === "stepper" && dotActive
      ? 0.22 + stepperLevel * 0.78
      : undefined;

  return (
    <div className="channel-row">
      <div className="channel-topline">
        <div className="channel-heading-text">
          <span
            aria-label={dotActive ? "Active" : "Inactive"}
            className={`channel-state-dot${dotActive ? " active" : ""}`}
            role="img"
            style={dotOpacity === undefined ? undefined : { opacity: dotOpacity }}
          />
          <span className="channel-title">Ch {channel.index + 1}</span>
          <button className="channel-map-target" type="button" onClick={onMap}>
            {isMapping ? "Click a jet..." : channelTargetLabel(channel)}
          </button>
        </div>
        <div className="channel-actions">
          {connection.type === "stepper" ? (
            <>
              <button
                className={`home-state-pill${channel.homed ? " homed" : " needs-home"}`}
                disabled={connection.port === null}
                type="button"
                onClick={onHome}
              >
                {channel.homed ? "Homed" : "Not Homed"}
              </button>
              <span
                className={`motion-state-pill${motionStateClassName(channel.motionState)}`}
              >
                {channel.motionState || "Unknown"}
              </span>
            </>
          ) : null}
        </div>
      </div>
      {connection.type === "stepper" ? (
        <>
          <div className="channel-metrics">
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
      ) : null}
    </div>
  );
}
