import type { PointerEvent as ReactPointerEvent } from "react";
import { EnvelopePanel } from "../envelope/EnvelopePanel";
import { ConnectionsSection } from "./ConnectionsSection";
import { ControlsSection } from "./ControlsSection";
import { LayoutSection } from "./LayoutSection";
import { setSidebarWidth, useSidebar } from "./sidebar-store";

export function LeftPane() {
  const { callbacks, snapshot } = useSidebar();

  function handleResizePointerDown(event: ReactPointerEvent<HTMLDivElement>) {
    event.preventDefault();
    const startX = event.clientX;
    const startWidth = snapshot.width;

    function handlePointerMove(moveEvent: PointerEvent) {
      setSidebarWidth(startWidth + moveEvent.clientX - startX);
      callbacks.onTransitionEnd();
    }

    function handlePointerUp() {
      window.removeEventListener("pointermove", handlePointerMove);
      window.removeEventListener("pointerup", handlePointerUp);
    }

    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp, { once: true });
  }

  return (
    <aside
      className={`sidebar${snapshot.collapsed ? " collapsed" : ""}`}
      id="sidebar"
      style={{
        marginLeft: snapshot.collapsed ? -snapshot.width : 0,
        width: snapshot.width,
      }}
      onTransitionEnd={callbacks.onTransitionEnd}
    >
      <div className="sidebar-header">
        <h1>Hexaflame</h1>
      </div>
      <div className="sidebar-body">
        <ControlsSection />
        <EnvelopePanel />
        <ConnectionsSection />
        <LayoutSection />
      </div>
      <div
        aria-hidden="true"
        className="pane-resize-handle pane-resize-handle-left"
        onPointerDown={handleResizePointerDown}
      />
    </aside>
  );
}
