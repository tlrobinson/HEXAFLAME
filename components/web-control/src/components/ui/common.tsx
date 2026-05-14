import type { ReactNode } from "react";

export function ActionButton({
  children,
  className = "btn-compact",
  disabled = false,
  onClick,
}: {
  children: ReactNode;
  className?: string;
  disabled?: boolean;
  onClick?: () => void;
}) {
  return (
    <button className={className} disabled={disabled} type="button" onClick={onClick}>
      {children}
    </button>
  );
}

export function MetricText({ children }: { children: ReactNode }) {
  return <span className="metric-text">{children}</span>;
}
