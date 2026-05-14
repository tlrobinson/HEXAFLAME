import { HexCanvas } from "./features/canvas/HexCanvas";
import { AppLifecycle } from "./features/lifecycle/AppLifecycle";
import { RightPane } from "./features/logs/RightPane";
import { LeftPane } from "./features/sidebar/LeftPane";
import { SidebarToggle } from "./features/sidebar/SidebarToggle";

export function App() {
  return (
    <>
      <AppLifecycle />
      <LeftPane />
      <div className="main-area">
        <SidebarToggle />
        <div className="canvas-wrap">
          <HexCanvas />
        </div>
      </div>
      <RightPane />
    </>
  );
}
