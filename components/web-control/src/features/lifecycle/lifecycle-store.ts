type LifecycleCallbacks = {
  onBeforeUnload: () => void;
  onPageHide: () => void;
  onResize: () => void;
  onSerialDisconnect: (event: Event) => void;
};

const noop = () => {};

let callbacks: LifecycleCallbacks = {
  onBeforeUnload: noop,
  onPageHide: noop,
  onResize: noop,
  onSerialDisconnect: noop,
};

export function getLifecycleCallbacks() {
  return callbacks;
}

export function setLifecycleCallbacks(nextCallbacks: LifecycleCallbacks) {
  callbacks = nextCallbacks;
}
