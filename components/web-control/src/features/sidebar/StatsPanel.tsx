import { Section } from "../../components/ui/Section";
import { useStats } from "./stats-store";

export function StatsPanel() {
  const stats = useStats();

  return (
    <Section>
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
