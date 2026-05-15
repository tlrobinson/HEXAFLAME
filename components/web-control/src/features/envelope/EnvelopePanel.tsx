import { Section } from "../../components/ui/Section";
import { useEnvelope } from "./envelope-store";

export function EnvelopePanel() {
  const envelope = useEnvelope();

  return (
    <Section label="Envelope">
      <svg
        aria-label="Stepper ADSR envelope"
        className="envelope-graph"
        id="stepper-envelope-graph"
        preserveAspectRatio="xMidYMid meet"
        viewBox="0 0 260 92"
      >
        <path className="envelope-grid" d="M0 70.5H260" />
        <path className="envelope-grid" d="M0 46.5H260" />
        <path className="envelope-grid" d="M0 22.5H260" />
        <path className="envelope-fill" d={envelope.fillPath} />
        <path className="envelope-path" d={envelope.path} />
        <line
          className="envelope-sweep"
          x1={envelope.sweepX}
          x2={envelope.sweepX}
          y1={envelope.sweepY1}
          y2={envelope.sweepY2}
        />
        <circle
          className="envelope-marker"
          cx={envelope.markerX}
          cy={envelope.markerY}
          r="3.5"
        />
      </svg>
      <div className="envelope-note-label">{envelope.noteLabel}</div>
      <div className="param-row">
        <div className="param-text">{envelope.attackLabel}</div>
        <div className="param-text">{envelope.decayLabel}</div>
        <div className="param-text">{envelope.sustainLabel}</div>
        <div className="param-text">{envelope.releaseLabel}</div>
      </div>
    </Section>
  );
}
