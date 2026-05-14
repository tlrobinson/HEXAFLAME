import { Section } from "../../components/ui/Section";
import { useGridControls } from "./grid-store";
import { useStats } from "./stats-store";

export function LayoutSection() {
  const { callbacks, snapshot } = useGridControls();
  const stats = useStats();

  return (
    <Section label="Layout">
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
          aria-label="Jet types"
          value={snapshot.jetMode}
          onChange={(event) => callbacks.onJetModeChange(event.currentTarget.value)}
        >
          <option value="all">Outlines + Spokes</option>
          <option value="outlines">Outlines Only</option>
          <option value="spokes">Spokes Only</option>
        </select>
      </div>
      <div className="section-row-wrap">
        <button type="button" onClick={callbacks.onReset}>Reset</button>
      </div>
      <div className="legend">
        <span><i className="swatch" style={{ background: "var(--blue)" }} /> Outlines</span>
        <span><i className="swatch" style={{ background: "var(--red)" }} /> Spokes</span>
      </div>
      <div className="stats" style={{ marginTop: 8 }}>
        <div className="stat-card">
          <strong>Outlines</strong>
          <div>Visible {stats.outline.visible} / Total {stats.outline.total}</div>
        </div>
        <div className="stat-card">
          <strong>Spokes</strong>
          <div>Visible {stats.spoke.visible} / Total {stats.spoke.total}</div>
        </div>
      </div>
    </Section>
  );
}
