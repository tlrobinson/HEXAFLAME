import { EnvelopePanel } from "../envelope/EnvelopePanel";
import { AnimationControls } from "./AnimationControls";
import { ConnectionsSection } from "./ConnectionsSection";
import { GridControls } from "./GridControls";
import { ScriptEditorPanel } from "./ScriptEditorPanel";
import { StatsPanel } from "./StatsPanel";
import { useSidebar } from "./sidebar-store";

export function LeftSidebar() {
  const { callbacks, snapshot } = useSidebar();

  return (
    <aside
      className={`sidebar${snapshot.collapsed ? " collapsed" : ""}`}
      id="sidebar"
      onTransitionEnd={callbacks.onTransitionEnd}
    >
      <div className="sidebar-header">
        <h1>Hexaflame</h1>
      </div>
      <div className="sidebar-body">
        <EnvelopePanel />
        <GridControls />
        <AnimationControls />
        <ScriptEditorPanel />
        <ConnectionsSection />
        <StatsPanel />
      </div>
    </aside>
  );
}
