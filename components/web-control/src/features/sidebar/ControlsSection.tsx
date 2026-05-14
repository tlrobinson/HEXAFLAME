import { useSyncExternalStore } from "react";
import { Section } from "../../components/ui/Section";
import {
  sequenceOptions,
  useAnimationControls,
} from "../animation/animation-controls-store";
import {
  getLogPaneSnapshot,
  setLogPaneCollapsed,
  setLogPaneTab,
  subscribeToLogPane,
} from "../logs/log-store";
import { useGridControls } from "./grid-store";

export function ControlsSection() {
  const { callbacks: animationCallbacks, snapshot: animation } =
    useAnimationControls();
  const { callbacks: gridCallbacks, snapshot: grid } = useGridControls();
  const logPane = useSyncExternalStore(
    subscribeToLogPane,
    getLogPaneSnapshot,
    getLogPaneSnapshot,
  );

  function openEditor() {
    setLogPaneTab("editor");
    setLogPaneCollapsed(false);
  }

  return (
    <Section label="Controls">
      <div className="section-row animation-command-row">
        <select
          aria-label="Animation script"
          value={animation.selectedSequenceId}
          onChange={(event) => animationCallbacks.onSequenceChange(event.currentTarget.value)}
        >
          {sequenceOptions.map((option) => (
            <option key={option.value} value={option.value}>{option.label}</option>
          ))}
        </select>
        <button
          aria-label={animation.playing ? "Pause animation" : "Play animation"}
          className="btn-icon btn-accent"
          title={animation.playing ? "Pause" : "Play"}
          type="button"
          onClick={animationCallbacks.onPlayPause}
        >
          <span aria-hidden="true">{animation.playing ? "⏸" : "▶"}</span>
        </button>
        <button
          aria-label={animation.loopEnabled ? "Disable loop" : "Enable loop"}
          className={`btn-icon${animation.loopEnabled ? " active" : ""}`}
          title={animation.loopEnabled ? "Loop on" : "Loop off"}
          type="button"
          onClick={animationCallbacks.onLoopToggle}
        >
          <span aria-hidden="true">↻</span>
        </button>
        <button
          aria-label="Edit script"
          className={`btn-icon${logPane.activeTab === "editor" && !logPane.collapsed ? " active" : ""}`}
          title="Edit script"
          type="button"
          onClick={openEditor}
        >
          <span aria-hidden="true">✎</span>
        </button>
      </div>

      <div className="section-row-wrap control-command-row">
        <button type="button" onClick={gridCallbacks.onAllOn}>All On</button>
        <button type="button" onClick={gridCallbacks.onAllOff}>All Off</button>
        <select
          aria-label="Label mode"
          className="control-label-select"
          value={grid.labelMode}
          onChange={(event) => gridCallbacks.onLabelModeChange(event.currentTarget.value)}
        >
          <option value="none">No Labels</option>
          <option value="address">Address Labels</option>
          <option value="distance">Distance Labels</option>
          <option value="channel">Channel Labels</option>
        </select>
      </div>

      <div className="section-row">
        <label htmlFor="speed-slider">Speed</label>
        <input
          id="speed-slider"
          max="18"
          min="1"
          type="range"
          value={animation.speed}
          onChange={(event) => animationCallbacks.onSpeedChange(Number(event.currentTarget.value))}
        />
        <output className="speed-readout" id="speed-readout" htmlFor="speed-slider">
          {animation.speed.toFixed(1)} steps/s
        </output>
      </div>

    </Section>
  );
}
