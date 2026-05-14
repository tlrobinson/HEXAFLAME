import { useEffect } from "react";
import { getLifecycleCallbacks } from "./lifecycle-store";

type NavigatorWithSerial = Navigator & {
  serial?: EventTarget;
};

export function AppLifecycle() {
  useEffect(() => {
    const callbacks = getLifecycleCallbacks();
    const serial = (navigator as NavigatorWithSerial).serial;

    serial?.addEventListener("disconnect", callbacks.onSerialDisconnect);
    window.addEventListener("pagehide", callbacks.onPageHide);
    window.addEventListener("beforeunload", callbacks.onBeforeUnload);
    window.addEventListener("resize", callbacks.onResize);

    return () => {
      serial?.removeEventListener("disconnect", callbacks.onSerialDisconnect);
      window.removeEventListener("pagehide", callbacks.onPageHide);
      window.removeEventListener("beforeunload", callbacks.onBeforeUnload);
      window.removeEventListener("resize", callbacks.onResize);
    };
  }, []);

  return null;
}
