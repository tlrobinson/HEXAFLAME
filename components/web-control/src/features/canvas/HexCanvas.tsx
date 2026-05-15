import {
  getCanvasCallbacks,
  setCanvasElement,
} from "./canvas-store";

export function HexCanvas() {
  const callbacks = getCanvasCallbacks();

  return (
    <canvas
      id="hex-canvas"
      ref={setCanvasElement}
      onMouseLeave={callbacks.onMouseLeave}
      onMouseMove={callbacks.onMouseMove}
      onPointerDown={callbacks.onPointerDown}
      onPointerUp={callbacks.onPointerUp}
    />
  );
}
