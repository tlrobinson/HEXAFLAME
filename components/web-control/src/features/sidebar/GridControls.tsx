import { Section } from "../../components/ui/Section";
import { useGridControls } from "./grid-store";

export function GridControls() {
  const { callbacks, snapshot } = useGridControls();

  return (
    <Section label="Grid">
      <div className="section-row">
        <label htmlFor="rings">Rings</label>
        <input
          id="rings"
          max="12"
          min="1"
          type="range"
          value={snapshot.rings}
          onChange={(event) => callbacks.onRingsChange(Number(event.currentTarget.value))}
        />
        <output id="ring-count" htmlFor="rings">{snapshot.rings}</output>
      </div>
      <div className="section-row">
        <select
          id="jet-mode-select"
          aria-label="Jet types"
          value={snapshot.jetMode}
          onChange={(event) => callbacks.onJetModeChange(event.currentTarget.value)}
        >
          <option value="all">Outlines + Spokes</option>
          <option value="outlines">Outlines Only</option>
          <option value="spokes">Spokes Only</option>
        </select>
      </div>
      <div className="section-row">
        <select
          id="label-mode-select"
          aria-label="Label mode"
          value={snapshot.labelMode}
          onChange={(event) => callbacks.onLabelModeChange(event.currentTarget.value)}
        >
          <option value="none">No Labels</option>
          <option value="address">Address Labels</option>
          <option value="distance">Distance Labels</option>
          <option value="channel">Channel Labels</option>
        </select>
      </div>
      <div className="section-row-wrap">
        <button type="button" onClick={callbacks.onAllOn}>All On</button>
        <button type="button" onClick={callbacks.onAllOff}>All Off</button>
        <button type="button" onClick={callbacks.onReset}>Reset</button>
      </div>
    </Section>
  );
}
