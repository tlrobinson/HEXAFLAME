import type { MouseEvent, PointerEvent } from "react";

type CanvasCallbacks = {
  onPointerDown: (event: PointerEvent<HTMLCanvasElement>) => void;
  onPointerUp: () => void;
  onMouseLeave: () => void;
  onMouseMove: (event: MouseEvent<HTMLCanvasElement>) => void;
};

const noop = () => {};

let canvasElement: HTMLCanvasElement | null = null;
let callbacks: CanvasCallbacks = {
  onPointerDown: noop,
  onPointerUp: noop,
  onMouseLeave: noop,
  onMouseMove: noop,
};

export function getCanvasElement() {
  return canvasElement;
}

export function setCanvasElement(element: HTMLCanvasElement | null) {
  canvasElement = element;
}

export function getCanvasCallbacks() {
  return callbacks;
}

export function setCanvasCallbacks(nextCallbacks: CanvasCallbacks) {
  callbacks = nextCallbacks;
}
