import { useSidebar } from "./sidebar-store";

export function SidebarToggle() {
  const { callbacks, snapshot } = useSidebar();

  return (
    <button
      aria-expanded={!snapshot.collapsed}
      aria-label="Toggle sidebar"
      className="sidebar-toggle"
      title="Toggle sidebar"
      type="button"
      onClick={callbacks.onToggle}
    >
      &#9776;
    </button>
  );
}
