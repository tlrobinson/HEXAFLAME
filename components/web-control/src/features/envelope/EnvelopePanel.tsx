import type { PointerEvent } from "react";
import { Section } from "../../components/ui/Section";
import { useEnvelope } from "./envelope-store";

export function EnvelopePanel() {
  const { callbacks, snapshot: envelope } = useEnvelope();

  function svgPointFromPointer(
    event: PointerEvent<SVGElement>,
    svg: SVGSVGElement,
  ) {
    const point = svg.createSVGPoint();
    point.x = event.clientX;
    point.y = event.clientY;
    const screenMatrix = svg.getScreenCTM();
    return screenMatrix
      ? point.matrixTransform(screenMatrix.inverse())
      : { x: 0, y: 0 };
  }

  function startDrag(
    handler: (point: { x: number; y: number }) => void,
  ) {
    return (event: PointerEvent<SVGElement>) => {
      const svg = event.currentTarget.ownerSVGElement || event.currentTarget;
      if (!(svg instanceof SVGSVGElement)) {
        return;
      }
      event.preventDefault();
      event.currentTarget.setPointerCapture(event.pointerId);
      handler(svgPointFromPointer(event, svg));
    };
  }

  function drag(
    handler: (point: { x: number; y: number }) => void,
  ) {
    return (event: PointerEvent<SVGElement>) => {
      if (!event.currentTarget.hasPointerCapture(event.pointerId)) {
        return;
      }
      const svg = event.currentTarget.ownerSVGElement || event.currentTarget;
      if (!(svg instanceof SVGSVGElement)) {
        return;
      }
      handler(svgPointFromPointer(event, svg));
    };
  }

  const onAttackDrag = (point: { x: number }) => callbacks.onAttackChange(point.x);
  const onDecayDrag = (point: { x: number }) => callbacks.onDecayChange(point.x);
  const onReleaseDrag = (point: { x: number }) => callbacks.onReleaseChange(point.x);
  const onSustainDrag = (point: { y: number }) => callbacks.onSustainChange(point.y);

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
        <g
          className="envelope-handle"
          onPointerDown={startDrag(onAttackDrag)}
          onPointerMove={drag(onAttackDrag)}
        >
          <line className="envelope-handle-hit" x1={envelope.attackX} x2={envelope.attackX} y1="10" y2="82" />
          <line x1={envelope.attackX} x2={envelope.attackX} y1="10" y2="82" />
          <text x={envelope.attackX} y="89">A</text>
        </g>
        <g
          className="envelope-handle"
          onPointerDown={startDrag(onDecayDrag)}
          onPointerMove={drag(onDecayDrag)}
        >
          <line className="envelope-handle-hit" x1={envelope.decayX} x2={envelope.decayX} y1="10" y2="82" />
          <line x1={envelope.decayX} x2={envelope.decayX} y1="10" y2="82" />
          <text x={envelope.decayX} y="89">D</text>
        </g>
        <g
          className="envelope-handle"
          onPointerDown={startDrag(onReleaseDrag)}
          onPointerMove={drag(onReleaseDrag)}
        >
          <line className="envelope-handle-hit" x1={envelope.releaseX} x2={envelope.releaseX} y1="10" y2="82" />
          <line x1={envelope.releaseX} x2={envelope.releaseX} y1="10" y2="82" />
          <text x={envelope.releaseX} y="89">R</text>
        </g>
        <g
          className="envelope-handle envelope-handle-sustain"
          onPointerDown={startDrag(onSustainDrag)}
          onPointerMove={drag(onSustainDrag)}
        >
          <line className="envelope-handle-hit" x1="14" x2="246" y1={envelope.sustainY} y2={envelope.sustainY} />
          <line x1="14" x2="246" y1={envelope.sustainY} y2={envelope.sustainY} />
          <text x="252" y={envelope.sustainY}>S</text>
        </g>
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
      <div className="param-row">
        <div className="param-text">{envelope.attackLabel}</div>
        <div className="param-text">{envelope.decayLabel}</div>
        <div className="param-text">{envelope.sustainLabel}</div>
        <div className="param-text">{envelope.releaseLabel}</div>
      </div>
    </Section>
  );
}
