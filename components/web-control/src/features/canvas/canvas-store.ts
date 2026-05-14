import type { MouseEvent } from "react";

type CanvasCallbacks = {
  onClick: (event: MouseEvent<HTMLCanvasElement>) => void;
  onMouseLeave: () => void;
  onMouseMove: (event: MouseEvent<HTMLCanvasElement>) => void;
};

const noop = () => {};

let canvasElement: HTMLCanvasElement | null = null;
let callbacks: CanvasCallbacks = {
  onClick: noop,
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
