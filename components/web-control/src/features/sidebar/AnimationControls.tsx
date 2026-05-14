import { Section } from "../../components/ui/Section";
import {
  sequenceOptions,
  useAnimationControls,
} from "../animation/animation-controls-store";

export function AnimationControls() {
  const { callbacks, snapshot } = useAnimationControls();

  return (
    <Section label="Animation">
      <div className="section-row">
        <select
          id="sequence-select"
          aria-label="Animation sequence"
          value={snapshot.selectedSequenceId}
          onChange={(event) => callbacks.onSequenceChange(event.currentTarget.value)}
        >
          {sequenceOptions.map((option) => (
            <option key={option.value} value={option.value}>{option.label}</option>
          ))}
        </select>
      </div>
      <div className="section-row-wrap">
        <button className="btn-accent" type="button" onClick={callbacks.onPlayPause}>
          {snapshot.playing ? "Pause" : "Play"}
        </button>
        <button type="button" onClick={callbacks.onLoopToggle}>
          {snapshot.loopEnabled ? "Loop On" : "Loop Off"}
        </button>
      </div>
      <div className="section-row">
        <label htmlFor="speed-slider">Speed</label>
        <input
          id="speed-slider"
          max="18"
          min="1"
          type="range"
          value={snapshot.speed}
          onChange={(event) => callbacks.onSpeedChange(Number(event.currentTarget.value))}
        />
        <output className="speed-readout" id="speed-readout" htmlFor="speed-slider">
          {snapshot.speed.toFixed(1)} steps/s
        </output>
      </div>
    </Section>
  );
}
