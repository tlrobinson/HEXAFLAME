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
      onClick={callbacks.onClick}
      onMouseLeave={callbacks.onMouseLeave}
      onMouseMove={callbacks.onMouseMove}
    />
  );
}
