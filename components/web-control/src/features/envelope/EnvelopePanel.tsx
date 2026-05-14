import { Section } from "../../components/ui/Section";

export function EnvelopePanel() {
  return (
    <Section label="Envelope">
      <div className="device-label">Center ADSR</div>
      <svg
        aria-label="Center ADSR envelope"
        className="envelope-graph"
        id="stepper-envelope-graph"
        preserveAspectRatio="none"
        viewBox="0 0 260 92"
      >
        <path className="envelope-grid" d="M0 70.5H260" />
        <path className="envelope-grid" d="M0 46.5H260" />
        <path className="envelope-grid" d="M0 22.5H260" />
        <path className="envelope-fill" id="stepper-envelope-fill" d="M14 78 L14 78 L14 78 Z" />
        <path className="envelope-path" id="stepper-envelope-path" d="M14 78 L70 14 L124 40 L190 40 L246 78" />
        <line className="envelope-sweep" id="stepper-envelope-sweep" x1="14" x2="14" y1="78" y2="78" />
        <circle className="envelope-marker" id="stepper-envelope-marker" cx="14" cy="78" r="3.5" />
      </svg>
      <div className="param-row">
        <div className="param-text" id="stepper-attack-readout">A 180ms</div>
        <div className="param-text" id="stepper-decay-readout">D 220ms</div>
        <div className="param-text" id="stepper-sustain-readout">S 55%</div>
        <div className="param-text" id="stepper-release-readout">R 320ms</div>
      </div>
    </Section>
  );
}
